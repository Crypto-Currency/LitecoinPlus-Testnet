// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "db.h"
#include "net.h"
#include "checkpoints.h"
#include "util.h"
#include "ui_interface.h"
#include "main.h"
#include "kernel.h"
#include <boost/version.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#ifndef WIN32
#include "sys/stat.h"
#endif

using namespace std;
using namespace boost;

extern CClientUIInterface uiInterface;
extern bool txIndexFileExists;
extern bool duringConversion;

unsigned int nWalletDBUpdated;

// by Simone: extend the class for conversion for >= 4.1.0.1
class CDiskBlockIndexV3Conv : public CDiskBlockIndexV3
{
public:
    IMPLEMENT_SERIALIZE
    (
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);

        READWRITE(hashNext);
        READWRITE(nFile);
        READWRITE(nBlockPos);
        READWRITE(nHeight);
        READWRITE(nMint);
        READWRITE(nMoneySupply);
        READWRITE(nFlags);
        READWRITE(nStakeModifier);
        if (const_cast<CDiskBlockIndexV3Conv*>(this)->IsProofOfStake())
        {
            READWRITE(prevoutStake);
            READWRITE(nStakeTime);
            READWRITE(hashProofOfStake);
        }
        else if (fRead)
        {
            const_cast<CDiskBlockIndexV3Conv*>(this)->prevoutStake.SetNull();
            const_cast<CDiskBlockIndexV3Conv*>(this)->nStakeTime = 0;
            const_cast<CDiskBlockIndexV3Conv*>(this)->hashProofOfStake = 0;
        }

        // block header
        READWRITE(this->nVersion);
        READWRITE(hashPrev);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);

		// add the hash of the current block
		READWRITE(hash);
    )
};


// by Simone: startup boost class gone, just use the destroy function from now
class boostStartup
{
public:
	// constructor
	boostStartup()
	{
	}

	// destructor
	~boostStartup()
	{
	}

	// deletes all stored boost files on disk
	void destroy()
	{
		char fName[64];

		for (int i = 0; i < 8; i++)
		{
			sprintf(fName, "bindex%04d.dat", i);
			boost::filesystem::path path = GetDataDir() / fName;
			if (boost::filesystem::exists(path))
			{
				boost::filesystem::remove(path);
			}
		}
	}
};


//
// CDB
//

CDBEnv bitdb;

void CDBEnv::EnvShutdown()
{
    if (!fDbEnvInit)
        return;

    fDbEnvInit = false;
    int ret = dbenv.close(0);
    if (ret != 0)
        printf("EnvShutdown exception: %s (%d)\n", DbEnv::strerror(ret), ret);
    if (!fMockDb)
        DbEnv(0).remove(strPath.c_str(), 0);
}

CDBEnv::CDBEnv() : dbenv(DB_CXX_NO_EXCEPTIONS)
{
    fDbEnvInit = false;
    fMockDb = false;
}

CDBEnv::~CDBEnv()
{
    EnvShutdown();
}

void CDBEnv::Close()
{
    EnvShutdown();
}

bool CDBEnv::Open(boost::filesystem::path pathEnv_)
{
    if (fDbEnvInit)
        return true;

    if (fShutdown)
        return false;

    pathEnv = pathEnv_;
    filesystem::path pathDataDir = pathEnv;
    strPath = pathDataDir.string();
    filesystem::path pathLogDir = pathDataDir / "database";
    filesystem::create_directory(pathLogDir);
    filesystem::path pathErrorFile = pathDataDir / "db.log";
    printf("dbenv.open LogDir=%s ErrorFile=%s\n", pathLogDir.string().c_str(), pathErrorFile.string().c_str());

    unsigned int nEnvFlags = 0;
    if (GetBoolArg("-privdb", true))
        nEnvFlags |= DB_PRIVATE;

    int nDbCache = GetArg("-dbcache", 25);
    dbenv.set_lg_dir(pathLogDir.string().c_str());
    dbenv.set_cachesize(nDbCache / 1024, (nDbCache % 1024)*1048576, 1);
    dbenv.set_lg_bsize(1048576);
    dbenv.set_lg_max(10485760);
    dbenv.set_lk_max_locks(10000);
    dbenv.set_lk_max_objects(10000);
    dbenv.set_errfile(fopen(pathErrorFile.string().c_str(), "a")); /// debug
    dbenv.set_flags(DB_AUTO_COMMIT, 1);
    dbenv.set_flags(DB_TXN_WRITE_NOSYNC, 1);
//    dbenv.log_set_config(DB_LOG_AUTO_REMOVE, 1);
    int ret = dbenv.open(strPath.c_str(),
                     DB_CREATE     |
                     DB_INIT_LOCK  |
                     DB_INIT_LOG   |
                     DB_INIT_MPOOL |
                     DB_INIT_TXN   |
                     DB_THREAD     |
                     DB_RECOVER    |
                     nEnvFlags,
                     S_IRUSR | S_IWUSR);
    if (ret != 0)
        return error("CDB() : error %s (%d) opening database environment", DbEnv::strerror(ret), ret);

    fDbEnvInit = true;
    fMockDb = false;
    return true;
}

void CDBEnv::MakeMock()
{
    if (fDbEnvInit)
        throw runtime_error("CDBEnv::MakeMock(): already initialized");

    if (fShutdown)
        throw runtime_error("CDBEnv::MakeMock(): during shutdown");

    printf("CDBEnv::MakeMock()\n");

    dbenv.set_cachesize(1, 0, 1);
    dbenv.set_lg_bsize(10485760*4);
    dbenv.set_lg_max(10485760);
    dbenv.set_lk_max_locks(10000);
    dbenv.set_lk_max_objects(10000);
    dbenv.set_flags(DB_AUTO_COMMIT, 1);
//    dbenv.log_set_config(DB_LOG_IN_MEMORY, 1);
    int ret = dbenv.open(NULL,
                     DB_CREATE     |
                     DB_INIT_LOCK  |
                     DB_INIT_LOG   |
                     DB_INIT_MPOOL |
                     DB_INIT_TXN   |
                     DB_THREAD     |
                     DB_PRIVATE,
                     S_IRUSR | S_IWUSR);
    if (ret > 0)
        throw runtime_error(strprintf("CDBEnv::MakeMock(): error %d opening database environment", ret));

    fDbEnvInit = true;
    fMockDb = true;
}

CDBEnv::VerifyResult CDBEnv::Verify(std::string strFile, bool (*recoverFunc)(CDBEnv& dbenv, std::string strFile))
{
    LOCK(cs_db);
    assert(mapFileUseCount.count(strFile) == 0);

    Db db(&dbenv, 0);
    int result = db.verify(strFile.c_str(), NULL, NULL, 0);
    if (result == 0)
        return VERIFY_OK;
    else if (recoverFunc == NULL)
        return RECOVER_FAIL;

    // Try to recover:
    bool fRecovered = (*recoverFunc)(*this, strFile);
    return (fRecovered ? RECOVER_OK : RECOVER_FAIL);
}

bool CDBEnv::Salvage(std::string strFile, bool fAggressive,
                     std::vector<CDBEnv::KeyValPair >& vResult)
{
    LOCK(cs_db);
    assert(mapFileUseCount.count(strFile) == 0);

    u_int32_t flags = DB_SALVAGE;
    if (fAggressive) flags |= DB_AGGRESSIVE;

    stringstream strDump;

    Db db(&dbenv, 0);
    int result = db.verify(strFile.c_str(), NULL, &strDump, flags);
    if (result != 0)
    {
        printf("ERROR: db salvage failed\n");
        return false;
    }

    // Format of bdb dump is ascii lines:
    // header lines...
    // HEADER=END
    // hexadecimal key
    // hexadecimal value
    // ... repeated
    // DATA=END

    string strLine;
    while (!strDump.eof() && strLine != "HEADER=END")
        getline(strDump, strLine); // Skip past header

    std::string keyHex, valueHex;
    while (!strDump.eof() && keyHex != "DATA=END")
    {
        getline(strDump, keyHex);
        if (keyHex != "DATA_END")
        {
            getline(strDump, valueHex);
            vResult.push_back(make_pair(ParseHex(keyHex),ParseHex(valueHex)));
        }
    }

    return (result == 0);
}


void CDBEnv::CheckpointLSN(std::string strFile)
{
    dbenv.txn_checkpoint(0, 0, 0);
    if (fMockDb)
        return;
    dbenv.lsn_reset(strFile.c_str(), 0);
}


CDB::CDB(const char *pszFile, const char* pszMode) :
    pdb(NULL), activeTxn(NULL)
{
	// by Simone: setting the below to true will output operational timing in console
	traceTiming = false;
    int ret;
    if (pszFile == NULL)
        return;

    fReadOnly = (!strchr(pszMode, '+') && !strchr(pszMode, 'w'));
    bool fCreate = strchr(pszMode, 'c');
    unsigned int nFlags = DB_THREAD;
    if (fCreate)
        nFlags |= DB_CREATE;

    {
        LOCK(bitdb.cs_db);
        if (!bitdb.Open(GetDataDir()))
            throw runtime_error("env open failed");

        strFile = pszFile;
        ++bitdb.mapFileUseCount[strFile];
        pdb = bitdb.mapDb[strFile];
        if (pdb == NULL)
        {
            pdb = new Db(&bitdb.dbenv, 0);

            bool fMockDb = bitdb.IsMock();
            if (fMockDb)
            {
                DbMpoolFile*mpf = pdb->get_mpf();
                ret = mpf->set_flags(DB_MPOOL_NOFILE, 1);
                if (ret != 0)
                    throw runtime_error(strprintf("CDB() : failed to configure for no temp file backing for database %s", pszFile));
            }

            ret = pdb->open(NULL,      // Txn pointer
                            fMockDb ? NULL : pszFile,   // Filename
                            "main",    // Logical db name
                            DB_BTREE,  // Database type
                            nFlags,    // Flags
                            0);

            if (ret != 0)
            {
                delete pdb;
                pdb = NULL;
                --bitdb.mapFileUseCount[strFile];
                strFile = "";
                throw runtime_error(strprintf("CDB() : can't open database file %s, error %d", pszFile, ret));
            }

            if (fCreate && !Exists(string("version")))
            {
                bool fTmp = fReadOnly;
                fReadOnly = false;
                WriteVersion(CLIENT_VERSION);
                fReadOnly = fTmp;
            }

            bitdb.mapDb[strFile] = pdb;
        }
    }
}

static bool IsChainFile(std::string strFile)
{
    if ((strFile == "blkindex.dat") || (strFile == "txindex.dat"))
        return true;

    return false;
}

void CDB::Close()
{
    if (!pdb)
        return;
    if (activeTxn)
        activeTxn->abort();
    activeTxn = NULL;
    pdb = NULL;

    // Flush database activity from memory pool to disk log
    unsigned int nMinutes = 0;
    if (fReadOnly)
        nMinutes = 1;
    if (IsChainFile(strFile))
        nMinutes = 2;
    if (IsChainFile(strFile) && IsInitialBlockDownload())
        nMinutes = 5;

	//fprintf(stderr, "NMIN %s::%d\n", strFile.c_str(), nMinutes);

    bitdb.dbenv.txn_checkpoint(nMinutes ? GetArg("-dblogsize", 100)*1024 : 0, nMinutes, 0);

    {
        LOCK(bitdb.cs_db);
        --bitdb.mapFileUseCount[strFile];
    }
}

int CDB::ReadAtCursor(Dbc* pcursor, CDataStream& ssKey, CDataStream& ssValue, unsigned int fFlags)
{
    // Read at cursor
    Dbt datKey;
    if (fFlags == DB_FIRST || fFlags == DB_SET || fFlags == DB_SET_RANGE || fFlags == DB_GET_BOTH || fFlags == DB_GET_BOTH_RANGE)
    {
        datKey.set_data(&ssKey[0]);
        datKey.set_size(ssKey.size());
    }
    Dbt datValue;
    if (fFlags == DB_GET_BOTH || fFlags == DB_GET_BOTH_RANGE)
    {
        datValue.set_data(&ssValue[0]);
        datValue.set_size(ssValue.size());
    }
    datKey.set_flags(DB_DBT_MALLOC);
    datValue.set_flags(DB_DBT_MALLOC);
    int ret = pcursor->get(&datKey, &datValue, fFlags);
    if (ret != 0)
        return ret;
    else if (datKey.get_data() == NULL || datValue.get_data() == NULL)
        return 99999;

    // Convert to streams
    ssKey.SetType(SER_DISK);
    ssKey.clear();
    ssKey.write((char*)datKey.get_data(), datKey.get_size());
    ssValue.SetType(SER_DISK);
    ssValue.clear();
    ssValue.write((char*)datValue.get_data(), datValue.get_size());

    // Clear and free memory
    memset(datKey.get_data(), 0, datKey.get_size());
    memset(datValue.get_data(), 0, datValue.get_size());
    free(datKey.get_data());
    free(datValue.get_data());
    return 0;
}


void CDBEnv::CloseDb(const string& strFile)
{
    {
        LOCK(cs_db);
        if (mapDb[strFile] != NULL)
        {
            // Close the database handle
            Db* pdb = mapDb[strFile];
            pdb->close(0);
            delete pdb;
            mapDb[strFile] = NULL;
        }
    }
}

bool CDBEnv::RemoveDb(const string& strFile)
{
    this->CloseDb(strFile);

    LOCK(cs_db);
    int rc = dbenv.dbremove(NULL, strFile.c_str(), NULL, DB_AUTO_COMMIT);
    return (rc == 0);
}

bool CDB::Rewrite(const string& strFile, const char* pszSkip)
{
    while (!fShutdown)
    {
        {
            LOCK(bitdb.cs_db);
            if (!bitdb.mapFileUseCount.count(strFile) || bitdb.mapFileUseCount[strFile] == 0)
            {
                // Flush log data to the dat file
                bitdb.CloseDb(strFile);
                bitdb.CheckpointLSN(strFile);
                bitdb.mapFileUseCount.erase(strFile);

                bool fSuccess = true;
                printf("Rewriting %s...\n", strFile.c_str());
                string strFileRes = strFile + ".rewrite";
                { // surround usage of db with extra {}
                    CDB db(strFile.c_str(), "r");
                    Db* pdbCopy = new Db(&bitdb.dbenv, 0);

                    int ret = pdbCopy->open(NULL,                 // Txn pointer
                                            strFileRes.c_str(),   // Filename
                                            "main",    // Logical db name
                                            DB_BTREE,  // Database type
                                            DB_CREATE,    // Flags
                                            0);
                    if (ret > 0)
                    {
                        printf("Cannot create database file %s\n", strFileRes.c_str());
                        fSuccess = false;
                    }

                    Dbc* pcursor = db.GetCursor();
                    if (pcursor)
                        while (fSuccess)
                        {
                            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
                            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
                            int ret = db.ReadAtCursor(pcursor, ssKey, ssValue, DB_NEXT);
                            if (ret == DB_NOTFOUND)
                            {
                                pcursor->close();
                                break;
                            }
                            else if (ret != 0)
                            {
                                pcursor->close();
                                fSuccess = false;
                                break;
                            }
                            if (pszSkip &&
                                strncmp(&ssKey[0], pszSkip, std::min(ssKey.size(), strlen(pszSkip))) == 0)
                                continue;
                            if (strncmp(&ssKey[0], "\x07version", 8) == 0)
                            {
                                // Update version:
                                ssValue.clear();
                                ssValue << CLIENT_VERSION;
                            }
                            Dbt datKey(&ssKey[0], ssKey.size());
                            Dbt datValue(&ssValue[0], ssValue.size());
                            int ret2 = pdbCopy->put(NULL, &datKey, &datValue, DB_NOOVERWRITE);
                            if (ret2 > 0)
                                fSuccess = false;
                        }
                    if (fSuccess)
                    {
                        db.Close();
                        bitdb.CloseDb(strFile);
                        if (pdbCopy->close(0))
                            fSuccess = false;
                        delete pdbCopy;
                    }
                }
                if (fSuccess)
                {
                    Db dbA(&bitdb.dbenv, 0);
                    if (dbA.remove(strFile.c_str(), NULL, 0))
                        fSuccess = false;
                    Db dbB(&bitdb.dbenv, 0);
                    if (dbB.rename(strFileRes.c_str(), NULL, strFile.c_str(), 0))
                        fSuccess = false;
                }
                if (!fSuccess)
                    printf("Rewriting of %s FAILED!\n", strFileRes.c_str());
                return fSuccess;
            }
        }
        Sleep(100);
    }
    return false;
}

bool CDB::Compact()
{
    if (!pdb)
		return false;
	return pdb->compact(activeTxn, NULL, NULL, NULL, DB_FREE_SPACE, NULL);
}

void CDBEnv::Flush(bool fShutdown)
{
    int64 nStart = GetTimeMillis();
    // Flush log data to the actual data file
    //  on all files that are not in use
    printf("Flush(%s)%s\n", fShutdown ? "true" : "false", fDbEnvInit ? "" : " db not started");
    if (!fDbEnvInit)
        return;
    {
        LOCK(cs_db);
        map<string, int>::iterator mi = mapFileUseCount.begin();
        while (mi != mapFileUseCount.end())
        {
            string strFile = (*mi).first;
            int nRefCount = (*mi).second;
            printf("%s refcount=%d\n", strFile.c_str(), nRefCount);
            if (nRefCount == 0)
            {
                // Move log data to the dat file
                CloseDb(strFile);
                printf("%s checkpoint\n", strFile.c_str());
                dbenv.txn_checkpoint(0, 0, 0);
                if (!IsChainFile(strFile) || fDetachDB) {
                    printf("%s detach\n", strFile.c_str());
                    if (!fMockDb)
					{
                        dbenv.lsn_reset(strFile.c_str(), 0);
					}
                }
                printf("%s closed\n", strFile.c_str());
                mapFileUseCount.erase(mi++);
            }
            else
                mi++;
        }
        printf("DBFlush(%s)%s ended %15" PRI64d "ms\n", fShutdown ? "true" : "false", fDbEnvInit ? "" : " db not started", GetTimeMillis() - nStart);
        if (fShutdown)
        {
            char** listp;
            if (mapFileUseCount.empty())
            {
                dbenv.log_archive(&listp, DB_ARCH_REMOVE);
                Close();
            }
        }
    }
}


//
// CCacheDB
//

bool CCacheDB::WriteCacheIndexV3(CDiskBlockIndexV3* blockindex)
{
	bool res = Write(make_pair(string("blockindex"), blockindex->hash), *blockindex);
    return res; 
}

bool CCacheDB::ReadCacheIndexV3(uint256 hash, CDiskBlockIndexV3& blockindex)
{
    assert(!fClient);
    return Read(make_pair(string("blockindex"), hash), blockindex);
}


//
// CBlkDB
//

bool CBlkDB::WriteBlockIndex(const CDiskBlockIndex& blockindex, uint256 blockHash)
{
	blockHash = (blockHash != 0) ? blockHash : blockindex.GetBlockHash();
	return Write(make_pair(string("blockindex"), blockHash), blockindex);
}

bool CBlkDB::WriteBlockIndexV2(const CDiskBlockIndexV2& blockindex)
{
	bool res = Write(make_pair(string("blockindex"), blockindex.hash), blockindex);
    return res; 
}

bool CBlkDB::WriteBlockIndexV3(CDiskBlockIndexV3* blockindex)
{
	bool res = Write(make_pair(string("blockindex"), blockindex->hash), *blockindex);
    return res; 
}

bool CBlkDB::ReadBlockIndexV2(uint256 hash, CDiskBlockIndexV2& blockindex)
{
    assert(!fClient);
    return Read(make_pair(string("blockindex"), hash), blockindex);
}

bool CBlkDB::ReadBlockIndexV3(uint256 hash, CDiskBlockIndexV3& blockindex)
{
    assert(!fClient);
    return Read(make_pair(string("blockindex"), hash), blockindex);
}

bool CBlkDB::EraseBlockIndex(uint256 hash)
{
    assert(!fClient);
    return Erase(make_pair(string("blockindex"), hash));
}

CBlockIndexV2* InsertBlockIndex(uint256 hash)
{
    if (hash == 0)
        return NULL;

    // Return existing
    map<uint256, CBlockIndexV2*>::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end())
        return (*mi).second;

    // Create new
    CBlockIndexV2* pindexNew = new CBlockIndexV2();
    if (!pindexNew)
        throw runtime_error("LoadBlockIndex() : new CBlockIndexV2 failed");
    mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool CBlkDB::convertTo41Index()
{
    unsigned int tempcount=0;
    string tempmess;
    string mess = "Calculating best chain...";

   // Calculate bnChainTrust
    vector<pair<int, CBlockIndexV2*> > vSortedByHeight;
    vSortedByHeight.reserve(mapBlockIndex.size());
    BOOST_FOREACH(const PAIRTYPE(uint256, CBlockIndexV2*)& item, mapBlockIndex)
    {
        CBlockIndexV2* pindex = item.second;
        vSortedByHeight.push_back(make_pair(pindex->nHeight(), pindex));
      tempcount++;
      if(tempcount>=10000)
      {
        tempmess = "Converting pairs / " + boost::to_string(pindex) + " [DO NOT INTERRUPT]";
        uiInterface.InitMessage(_(tempmess.c_str()));
        tempcount=0;
      }
    }
    sort(vSortedByHeight.begin(), vSortedByHeight.end());
    BOOST_FOREACH(const PAIRTYPE(int, CBlockIndexV2*)& item, vSortedByHeight)
    {
        CBlockIndexV2* pindex = item.second;
		CDiskBlockIndexV3* diskindex = pindex->getDiskAccess(true);
        diskindex->bnChainTrust = (pindex->pprev ? pindex->pprev->bnChainTrust() : 0) + pindex->GetBlockTrust();
      tempcount++;
      if(tempcount>=30000)
      {
        tempmess = "Storing stake modifiers / " + pindex->bnChainTrust().ToString() + " [DO NOT INTERRUPT]";
        uiInterface.InitMessage(_(tempmess.c_str()));
        tempcount=0;
      }
        // ppcoin: calculate stake modifier checksum
        diskindex->nStakeModifierChecksum = GetStakeModifierChecksum(pindex);
        if (!CheckStakeModifierCheckpoints(pindex->nHeight(), pindex->nStakeModifierChecksum()))
            return error("CTxDB::LoadBlockIndex() : Failed stake modifier checkpoint height=%d, modifier=0x%016" PRI64x, pindex->nHeight(), pindex->nStakeModifier);
		WriteBlockIndexV3(diskindex);
		diskindex->uncommitted = false;
    }
	return true;
}

bool CBlkDB::LoadBlockIndex()
{
    if (!LoadBlockIndexGuts())
    	return false;

    if (fRequestShutdown)
		return true;

    unsigned int tempcount=0;
    unsigned int steptemp=0;
    string tempmess;
    string mess = "Calculating best chain...";
    uiInterface.InitMessage(_(mess.c_str()));
 
    // Load hashBestChain pointer to end of best chain
    if (!pTxdb->ReadHashBestChain(hashBestChain))
    {
        if (pindexGenesisBlock == NULL)
		{
            return true;
		}
        return error("CTxDB::LoadBlockIndex() : hashBestChain not loaded");
    }
    if (!mapBlockIndex.count(hashBestChain))
        return error("CTxDB::LoadBlockIndex() : hashBestChain not found in the block index");

    pindexBest = mapBlockIndex[hashBestChain];
    nBestHeight = pindexBest->nHeight();
    bnBestChainTrust = pindexBest->bnChainTrust();
    printf("LoadBlockIndex(): hashBestChain=%s  height=%d  trust=%s  date=%s\n",
      hashBestChain.ToString().substr(0,20).c_str(), nBestHeight, bnBestChainTrust.ToString().c_str(),
      DateTimeStrFormat("%x %H:%M:%S", pindexBest->GetBlockTime()).c_str());

    // ppcoin: load hashSyncCheckpoint
    if (!pTxdb->ReadSyncCheckpoint(Checkpoints::hashSyncCheckpoint))
        return error("CTxDB::LoadBlockIndex() : hashSyncCheckpoint not loaded");
    printf("LoadBlockIndex(): synchronized checkpoint %s\n", Checkpoints::hashSyncCheckpoint.ToString().c_str());

    // Load bnBestInvalidTrust, OK if it doesn't exist
    pTxdb->ReadBestInvalidTrust(bnBestInvalidTrust);
    // Verify blocks in the best chain
    int nCheckLevel = GetArg("-checklevel", 1);
    int nCheckDepth = GetArg( "-checkblocks", 2500);
    if (nCheckDepth == 0)
        nCheckDepth = 1000000000; // suffices until the year 19000
    if (nCheckDepth > nBestHeight)
        nCheckDepth = nBestHeight;
    printf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CBlockIndexV2* pindexFork = NULL;
    map<pair<unsigned int, unsigned int>, CBlockIndexV2*> mapBlockPos;


    for (CBlockIndexV2* pindex = pindexBest; pindex && pindex->pprev; pindex = pindex->pprev)
    {
    tempcount++;
    if(tempcount>=100)
    {
      steptemp ++;
      tempmess=mess+" / "+ boost::to_string(pindex);
      uiInterface.InitMessage(_(tempmess.c_str()));
      tempcount=0;
    }
        if (fRequestShutdown || pindex->nHeight() < nBestHeight-nCheckDepth)
            break;
        CBlock block;
        if (!block.ReadFromDisk(pindex))
            return error("LoadBlockIndex() : block.ReadFromDisk failed");
        // check level 1: verify block validity
        if (nCheckLevel>0 && !block.CheckBlock())
        {
            printf("LoadBlockIndex() : *** found bad block at %d, hash=%s\n", pindex->nHeight(), pindex->GetBlockHash().ToString().c_str());
            pindexFork = pindex->pprev;
        }
        // check level 2: verify transaction index validity
        if (nCheckLevel>1)
        {
            pair<unsigned int, unsigned int> pos = make_pair(pindex->nFile(), pindex->nBlockPos());
            mapBlockPos[pos] = pindex;
            BOOST_FOREACH(const CTransaction &tx, block.vtx)
            {
                uint256 hashTx = tx.GetHash();
                CTxIndex txindex;
                if (pTxdb->ReadTxIndex(hashTx, txindex))
                {
                    // check level 3: checker transaction hashes
                    if (nCheckLevel>2 || pindex->nFile() != txindex.pos.nFile || pindex->nBlockPos() != txindex.pos.nBlockPos)
                    {
                        // either an error or a duplicate transaction
                        CTransaction txFound;
                        if (!txFound.ReadFromDisk(txindex.pos))
                        {
                            printf("LoadBlockIndex() : *** cannot read mislocated transaction %s\n", hashTx.ToString().c_str());
                            pindexFork = pindex->pprev;
                        }
                        else
                            if (txFound.GetHash() != hashTx) // not a duplicate tx
                            {
                                printf("LoadBlockIndex(): *** invalid tx position for %s\n", hashTx.ToString().c_str());
                                pindexFork = pindex->pprev;
                            }
                    }
                    // check level 4: check whether spent txouts were spent within the main chain
                    unsigned int nOutput = 0;
                    if (nCheckLevel>3)
                    {
                        BOOST_FOREACH(const CDiskTxPos &txpos, txindex.vSpent)
                        {
                            if (!txpos.IsNull())
                            {
                                pair<unsigned int, unsigned int> posFind = make_pair(txpos.nFile, txpos.nBlockPos);
                                if (!mapBlockPos.count(posFind))
                                {
                                    printf("LoadBlockIndex(): *** found bad spend at %d, hashBlock=%s, hashTx=%s\n", pindex->nHeight(), pindex->GetBlockHash().ToString().c_str(), hashTx.ToString().c_str());
                                    pindexFork = pindex->pprev;
                                }
                                // check level 6: check whether spent txouts were spent by a valid transaction that consume them
                                if (nCheckLevel>5)
                                {
                                    CTransaction txSpend;
                                    if (!txSpend.ReadFromDisk(txpos))
                                    {
                                        printf("LoadBlockIndex(): *** cannot read spending transaction of %s:%i from disk\n", hashTx.ToString().c_str(), nOutput);
                                        pindexFork = pindex->pprev;
                                    }
                                    else if (!txSpend.CheckTransaction())
                                    {
                                        printf("LoadBlockIndex(): *** spending transaction of %s:%i is invalid\n", hashTx.ToString().c_str(), nOutput);
                                        pindexFork = pindex->pprev;
                                    }
                                    else
                                    {
                                        bool fFound = false;
                                        BOOST_FOREACH(const CTxIn &txin, txSpend.vin)
                                            if (txin.prevout.hash == hashTx && txin.prevout.n == nOutput)
                                                fFound = true;
                                        if (!fFound)
                                        {
                                            printf("LoadBlockIndex(): *** spending transaction of %s:%i does not spend it\n", hashTx.ToString().c_str(), nOutput);
                                            pindexFork = pindex->pprev;
                                        }
                                    }
                                }
                            }
                            nOutput++;
                        }
                    }
                }
                // check level 5: check whether all prevouts are marked spent
                if (nCheckLevel>4)
                {
                     BOOST_FOREACH(const CTxIn &txin, tx.vin)
                     {
                          CTxIndex txindex;
                          if (pTxdb->ReadTxIndex(txin.prevout.hash, txindex))
                              if (txindex.vSpent.size()-1 < txin.prevout.n || txindex.vSpent[txin.prevout.n].IsNull())
                              {
                                  printf("LoadBlockIndex(): *** found unspent prevout %s:%i in %s\n", txin.prevout.hash.ToString().c_str(), txin.prevout.n, hashTx.ToString().c_str());
                                  pindexFork = pindex->pprev;
                              }
                     }
                }
            }
        }
    }
    if (pindexFork && !fRequestShutdown)
    {
        // Reorg back to the fork
        printf("LoadBlockIndex() : *** moving best chain pointer back to block %d\n", pindexFork->nHeight());
        CBlock block;
        if (!block.ReadFromDisk(pindexFork))
            return error("LoadBlockIndex() : block.ReadFromDisk failed");
        CTxDB txdb;
        block.SetBestChain(txdb, pindexFork);
    }
    return true;
}


u_int32_t CBlkDB::GetCount()
{
	// TO DO

    return 0;
}

// completely remove cached index from disk
void CBlkDB::DestroyCachedIndex()
{
	boostStartup *boost = new boostStartup();
	boost->destroy();
	delete boost;
}

bool CBlkDB::LoadBlockIndexGuts()
{
	// by Simone: if txindex do not exist, we need to split blkindex and txindex !
	if (!txIndexFileExists)
	{
		DestroyCachedIndex();
		pTxdb->SpliceTxIndex();
	}

    // loop control variables
	double ccc = 0;
	double cnt = 0;
	int oldProgress = -1;

	// instead of counting the number of records, which is pointless and VERY slow on old machines, we just set an approximate number
	cnt = (double)boost::filesystem::file_size(GetDataDir() / "blkindex.dat") / 337.0;
	oldProgress = -1;
	map<unsigned long, CBlockIndexV2 *> repairIndexes;

	// use bulk suggested way to loop entire DB
	DB *dbp = pdb->get_DB();
	DBC *dbcp;
	DBT key, data;
	size_t retklen, retdlen;
	unsigned char *retkey, *retdata;
	int ret;
	void *p;

	// identify the start of the loading process
	loading_process:

	// prepare for loop
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	#define	BUFFER_LENGTH	(128 * 1024 * 1024)
	if ((data.data = malloc(BUFFER_LENGTH)) == NULL)
		return false;
	data.ulen = BUFFER_LENGTH;
	data.flags = DB_DBT_USERMEM;

	// cursor the old way
	if ((ret = dbp->cursor(dbp, NULL, &dbcp, 0)) != 0) {
		free(data.data);
		return false;
	}

	// loop all data in big chunks
	bool needUpgrade41 = false;
	loop {

	// get the chunk
		if ((ret = dbcp->c_get(dbcp, &key, &data, DB_MULTIPLE_KEY | DB_NEXT)) != 0) {
			break;
		}

	// loop the chunk
		for (DB_MULTIPLE_INIT(p, &data);;) {
			DB_MULTIPLE_KEY_NEXT(p,
			    &data, retkey, retklen, retdata, retdlen);
			if (p == NULL)
				break;
			int progress = (ccc / cnt) * 100;
			if (progress > 100) {
				progress = 100;
			}
			if (progress != oldProgress) {
				char pString[256];
				if (needUpgrade41)
					sprintf(pString, (_("Converting index (%d%%)... [DO NOT INTERRUPT]")).c_str(), progress);
				else
					sprintf(pString, (_("Loading block index (%d%%)...")).c_str(), progress);
		#ifdef QT_GUI
				uiInterface.InitMessage(pString);
		#endif
				oldProgress = progress;
			}
			ccc += 1.0;

	// convert to streams
			CDataStream ssKey(SER_DISK, CLIENT_VERSION);
			CDataStream ssValue(SER_DISK, CLIENT_VERSION);
		    ssKey.SetType(SER_DISK);
		    ssKey.clear();
		    ssKey.write((char*)retkey, retklen);
		    ssValue.SetType(SER_DISK);
		    ssValue.clear();
		    ssValue.write((char*)retdata, (int)retdlen);
		    string strType;
		    ssKey >> strType;
		    if (strType == "blockindex" && !fRequestShutdown)
		    {

	// then into objects
		        CDiskBlockIndexV3 diskindex;
				CDiskBlockIndexV3Conv diskindexPrior41;

	// if already flagged for conversion, just read conversion data
				try {
			    	ssValue >> diskindex;
				} catch (std::exception &e) {

	// it may be necessary to convert the data from previous 4.1.0.1 versions, let's try to squeeze the stream like this
					try {
						CDataStream ssValue41(SER_DISK, CLIENT_VERSION);
						ssValue41.SetType(SER_DISK);
						ssValue41.clear();
						ssValue41.write((char*)retdata, (int)retdlen);
						ssValue41 >> diskindexPrior41;

	// if logic reaches here, then we need to updgrade after the loop
						needUpgrade41 = true;
					} catch (std::exception &e) {

	// nope, it really is an error, so we need to handle it
						return false;
					}
				}

    // construct block index object
		        CBlockIndexV2* pindexNew;
				uint256 hash;
				if (needUpgrade41)
				{
					duringConversion          = true;
					hash                      = diskindexPrior41.GetBlockHash();
				    pindexNew                 = InsertBlockIndex(hash);
				    pindexNew->pprev          = InsertBlockIndex(diskindexPrior41.hashPrev);
				    pindexNew->pnext          = InsertBlockIndex(diskindexPrior41.hashNext);
				    pindexNew->nFlags         = diskindexPrior41.nFlags;
				    pindexNew->nStakeModifier = diskindexPrior41.nStakeModifier;

	// during conversion, we load everything else too
					CDiskBlockIndexV3* diskaccess = pindexNew->getPureDiskAccess();
				    diskaccess->nFile          = diskindex.nFile;
				    diskaccess->nBlockPos      = diskindex.nBlockPos;
				    diskaccess->nHeight        = diskindex.nHeight;
				    diskaccess->nMint          = diskindex.nMint;
				    diskaccess->nMoneySupply   = diskindex.nMoneySupply;
				    diskaccess->prevoutStake   = diskindex.prevoutStake;
				    diskaccess->nStakeTime     = diskindex.nStakeTime;
				    diskaccess->hashProofOfStake = diskindex.hashProofOfStake;
				    diskaccess->nVersion       = diskindex.nVersion;
				    diskaccess->hashMerkleRoot = diskindex.hashMerkleRoot;
				    diskaccess->nTime          = diskindex.nTime;
				    diskaccess->nBits          = diskindex.nBits;
				    diskaccess->nNonce         = diskindex.nNonce;

	// detect empty hashes
					if (diskindexPrior41.hash == 0)
					{
						repairIndexes.insert(make_pair(ccc, pindexNew));
					}

    // watch for genesis block
				    if (pindexGenesisBlock == NULL && hash == (!fTestNet ? hashGenesisBlock : hashGenesisBlockTestNet))
				        pindexGenesisBlock = pindexNew;

    // ppcoin: build setStakeSeen
				    if (pindexNew->IsProofOfStake())
				        setStakeSeen.insert(make_pair(diskindexPrior41.prevoutStake, diskindexPrior41.nStakeTime));
				}
				else
				{
				    pindexNew                 = InsertBlockIndex(diskindex.GetBlockHash());
				    pindexNew->pprev          = InsertBlockIndex(diskindex.hashPrev);
				    pindexNew->pnext          = InsertBlockIndex(diskindex.hashNext);
				    pindexNew->nFlags         = diskindex.nFlags;
				    pindexNew->nStakeModifier = diskindex.nStakeModifier;

	// detect empty hashes
					if (diskindex.hash == 0)
					{
						repairIndexes.insert(make_pair(ccc, pindexNew));
					}

    // watch for genesis block
				    if (pindexGenesisBlock == NULL && diskindex.GetBlockHash() == (!fTestNet ? hashGenesisBlock : hashGenesisBlockTestNet))
				        pindexGenesisBlock = pindexNew;

    // ppcoin: build setStakeSeen
				    if (pindexNew->IsProofOfStake())
				        setStakeSeen.insert(make_pair(diskindex.prevoutStake, diskindex.nStakeTime));
				}
			}			
		}
	}
	dbcp->c_close(dbcp);
	free(data.data);

	// test 4.1.0.1 or above upgrade needed, then just do it
	if (needUpgrade41)
	{
		if (convertTo41Index())
		{
			duringConversion = false;
			goto loading_process;		// jump to the beginning of the loop, effectively loading once more the correct chain
		}
	}

	// now repair index if needed
#ifdef QT_GUI
	uiInterface.InitMessage((_("Repairing index...")).c_str());
#endif
	for (map<unsigned long, CBlockIndexV2 *>::iterator mi = repairIndexes.begin(); mi != repairIndexes.end(); ++mi)
	{
		CDiskBlockIndexV3 repairIndex((*mi).second);
		WriteBlockIndexV3(&repairIndex);
	}
	repairIndexes.clear();
	return true;
}


//
// CTxDB
//

bool CTxDB::ReadTxIndex(uint256 hash, CTxIndex& txindex)
{
    assert(!fClient);
    txindex.SetNull();
    return Read(make_pair(string("tx"), hash), txindex);
}

bool CTxDB::UpdateTxIndex(uint256 hash, const CTxIndex& txindex)
{
    assert(!fClient);
    return Write(make_pair(string("tx"), hash), txindex);
}

bool CTxDB::AddTxIndex(const CTransaction& tx, const CDiskTxPos& pos, int nHeight)
{
    assert(!fClient);

    // Add to tx index
    uint256 hash = tx.GetHash();
    CTxIndex txindex(pos, tx.vout.size());
    return Write(make_pair(string("tx"), hash), txindex);
}

bool CTxDB::EraseTxIndex(const CTransaction& tx)
{
    assert(!fClient);
    uint256 hash = tx.GetHash();

    return Erase(make_pair(string("tx"), hash));
}

bool CTxDB::ContainsTx(uint256 hash)
{
    assert(!fClient);
    return Exists(make_pair(string("tx"), hash));
}

bool CTxDB::ReadDiskTx(uint256 hash, CTransaction& tx, CTxIndex& txindex)
{
    assert(!fClient);
    tx.SetNull();
    if (!ReadTxIndex(hash, txindex))
        return false;
    return (tx.ReadFromDisk(txindex.pos));
}

bool CTxDB::ReadDiskTx(uint256 hash, CTransaction& tx)
{
    CTxIndex txindex;
    return ReadDiskTx(hash, tx, txindex);
}

bool CTxDB::ReadDiskTx(COutPoint outpoint, CTransaction& tx, CTxIndex& txindex)
{
    return ReadDiskTx(outpoint.hash, tx, txindex);
}

bool CTxDB::ReadDiskTx(COutPoint outpoint, CTransaction& tx)
{
    CTxIndex txindex;
    return ReadDiskTx(outpoint.hash, tx, txindex);
}

bool CTxDB::ReadHashBestChain(uint256& hashBestChain)
{
    return Read(string("hashBestChain"), hashBestChain);
}

bool CTxDB::WriteHashBestChain(uint256 hashBestChain)
{
    return Write(string("hashBestChain"), hashBestChain);
}

bool CTxDB::ReadBestInvalidTrust(CBigNum& bnBestInvalidTrust)
{
    return Read(string("bnBestInvalidTrust"), bnBestInvalidTrust);
}

bool CTxDB::WriteBestInvalidTrust(CBigNum bnBestInvalidTrust)
{
    return Write(string("bnBestInvalidTrust"), bnBestInvalidTrust);
}

bool CTxDB::ReadSyncCheckpoint(uint256& hashCheckpoint)
{
    return Read(string("hashSyncCheckpoint"), hashCheckpoint);
}

bool CTxDB::WriteSyncCheckpoint(uint256 hashCheckpoint)
{
    return Write(string("hashSyncCheckpoint"), hashCheckpoint);
}

bool CTxDB::ReadCheckpointPubKey(string& strPubKey)
{
    return Read(string("strCheckpointPubKey"), strPubKey);
}

bool CTxDB::WriteCheckpointPubKey(const string& strPubKey)
{
    return Write(string("strCheckpointPubKey"), strPubKey);
}

bool CTxDB::EraseBlockIndex(uint256 hash)
{
    assert(!fClient);
    return Erase(make_pair(string("tx"), hash));
}

bool CTxDB::SpliceTxIndex()
{
	// progress control variables
	unsigned int fFlags = DB_SET_RANGE;
	unsigned long ccc = 0;
	unsigned long cnt = 0;
	int oldProgress = -1;

	// execute the splice of the index in a nice, clean way
	Dbc* pcursor1 = GetCursor();
	if (!pcursor1)
		return false;

	// instead of counting the number of records, which is pointless and VERY slow on old machines, we just set an approximate number
	cnt = boost::filesystem::file_size(GetDataDir() / "txindex.dat") / 589;
	fFlags = DB_SET_RANGE;
	oldProgress = -1;
	map<unsigned long, uint256> eraseHashes;
	loop
	{
		// Read next record
		CDataStream ssKey(SER_DISK, CLIENT_VERSION);
		if (fFlags == DB_SET_RANGE) {
		    ssKey << make_pair(string("blockindex"), uint256(0));
		}
		CDataStream ssValue(SER_DISK, CLIENT_VERSION);
		int ret = ReadAtCursor(pcursor1, ssKey, ssValue, fFlags);
		fFlags = DB_NEXT;
		if (ret == DB_NOTFOUND)
			break;
		else if (ret != 0)
			return false;
		int progress = (int)(((double)(ccc) / (double)(cnt)) * 100);
		if (progress > 100) {
			progress = 100;
		}
		if (oldProgress != progress) {
			char pString[256];
			sprintf(pString, (_("Upgrading block index (SLOW, %d%%)...") + "   " + _("[DO NOT INTERRUPT]")).c_str(), progress);
#ifdef QT_GUI
			uiInterface.InitMessage(pString);
#endif
			oldProgress = progress;
		}
		ccc++;

		// Unserialize
		try {
			string strType;
			ssKey >> strType;
			if (strType == "blockindex" && !fRequestShutdown)
			{
				CDiskBlockIndex diskindex;
				ssValue >> diskindex;
				uint256 blockHash = diskindex.GetBlockHash();

				// save as v2 object
				CDiskBlockIndexV2 diskindexv2;

		        // Construct block index object v2
		        diskindexv2.hash           = blockHash;
		        diskindexv2.hashPrev       = diskindex.hashPrev;
		        diskindexv2.hashNext       = diskindex.hashNext;
		        diskindexv2.nFile          = diskindex.nFile;
		        diskindexv2.nBlockPos      = diskindex.nBlockPos;
		        diskindexv2.nHeight        = diskindex.nHeight;
		        diskindexv2.nMint          = diskindex.nMint;
		        diskindexv2.nMoneySupply   = diskindex.nMoneySupply;
		        diskindexv2.nFlags         = diskindex.nFlags;
		        diskindexv2.nStakeModifier = diskindex.nStakeModifier;
		        diskindexv2.prevoutStake   = diskindex.prevoutStake;
		        diskindexv2.nStakeTime     = diskindex.nStakeTime;
		        diskindexv2.hashProofOfStake = diskindex.hashProofOfStake;
		        diskindexv2.nVersion       = diskindex.nVersion;
		        diskindexv2.hashMerkleRoot = diskindex.hashMerkleRoot;
		        diskindexv2.nTime          = diskindex.nTime;
		        diskindexv2.nBits          = diskindex.nBits;
		        diskindexv2.nNonce         = diskindex.nNonce;

				// write new blockindex into the correct file blkindex.dat, and convert to version 2
				if (blkDb->WriteBlockIndexV2(diskindexv2))
				{

					// signal to erase blockindex entry into txindex.dat
					eraseHashes.insert(make_pair(ccc, blockHash));
				}
			}
			else
			{
				break; // if shutdown requested or finished loading block index
			}
		}    // try
		catch (std::exception &e) {
		    return error("%s() : deserialize error", __PRETTY_FUNCTION__);
		}
	}
	pcursor1->close();

	// now erase all stuff not needed from the txindex.dat file
#ifdef QT_GUI
	uiInterface.InitMessage((_("Cleaning up...")).c_str());
#endif
	for (map<unsigned long, uint256>::iterator mi = eraseHashes.begin(); mi != eraseHashes.end(); ++mi)
	{
		EraseBlockIndex((*mi).second);
	}

	// compact DB
	Compact();

	// exit with success
	return true;
}


//
// CAddrDB
//


CAddrDB::CAddrDB()
{
    pathAddr = GetDataDir() / "peers.dat";
}

bool CAddrDB::Write(const CAddrMan& addr)
{
    // Generate random temporary filename
    unsigned short randv = 0;
    RAND_bytes((unsigned char *)&randv, sizeof(randv));
    std::string tmpfn = strprintf("peers.dat.%04x", randv);

    // serialize addresses, checksum data up to that point, then append csum
    CDataStream ssPeers(SER_DISK, CLIENT_VERSION);
    ssPeers << FLATDATA(pchMessageStart);
    ssPeers << addr;
    uint256 hash = Hash(ssPeers.begin(), ssPeers.end());
    ssPeers << hash;

    // open temp output file, and associate with CAutoFile
    boost::filesystem::path pathTmp = GetDataDir() / tmpfn;
    FILE *file = fopen(pathTmp.string().c_str(), "wb");
    CAutoFile fileout = CAutoFile(file, SER_DISK, CLIENT_VERSION);
    if (!fileout)
        return error("CAddrman::Write() : open failed");

    // Write and commit header, data
    try {
        fileout << ssPeers;
    }
    catch (std::exception &e) {
        return error("CAddrman::Write() : I/O error");
    }
    FileCommit(fileout);
    fileout.fclose();

    // replace existing peers.dat, if any, with new peers.dat.XXXX
    if (!RenameOver(pathTmp, pathAddr))
        return error("CAddrman::Write() : Rename-into-place failed");

    return true;
}

bool CAddrDB::Read(CAddrMan& addr)
{
    // open input file, and associate with CAutoFile
    FILE *file = fopen(pathAddr.string().c_str(), "rb");
    CAutoFile filein = CAutoFile(file, SER_DISK, CLIENT_VERSION);
    if (!filein)
        return error("CAddrman::Read() : open failed");

    // use file size to size memory buffer
    int fileSize = GetFilesize(filein);
    int dataSize = fileSize - sizeof(uint256);
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char *)&vchData[0], dataSize);
        filein >> hashIn;
    }
    catch (std::exception &e) {
        return error("CAddrman::Read() 2 : I/O error or stream data corrupted");
    }
    filein.fclose();

    CDataStream ssPeers(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssPeers.begin(), ssPeers.end());
    if (hashIn != hashTmp)
        return error("CAddrman::Read() : checksum mismatch; data corrupted");

    unsigned char pchMsgTmp[4];
    try {
        // de-serialize file header (pchMessageStart magic number) and
        ssPeers >> FLATDATA(pchMsgTmp);

        // verify the network matches ours
        if (memcmp(pchMsgTmp, pchMessageStart, sizeof(pchMsgTmp)))
            return error("CAddrman::Read() : invalid network magic number");

        // de-serialize address data into one CAddrMan object
        ssPeers >> addr;
    }
    catch (std::exception &e) {
        return error("CAddrman::Read() : I/O error or stream data corrupted");
    }

    return true;
}

