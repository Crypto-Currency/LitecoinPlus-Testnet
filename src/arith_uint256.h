// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ARITH_UINT256_H
#define BITCOIN_ARITH_UINT256_H

#include <assert.h>
#include <cstring>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <vector>

uint64_t static inline ReadLE64(const unsigned char* ptr)
{
    uint64_t x;
    memcpy((char*)&x, ptr, 8);
    return le64toh(x);
}

void static inline WriteLE64(unsigned char* ptr, uint64_t x)
{
    uint64_t v = htole64(x);
    memcpy(ptr, (char*)&v, 8);
}

uint32_t static inline ReadLE32(const unsigned char* ptr)
{
    uint32_t x;
    memcpy((char*)&x, ptr, 4);
    return le32toh(x);
}

void static inline WriteLE32(unsigned char* ptr, uint32_t x)
{
    uint32_t v = htole32(x);
    memcpy(ptr, (char*)&v, 4);
}

/** Template base class for fixed-sized opaque blobs. */
template<unsigned int BITS>
class base_blob
{
protected:
    static constexpr int WIDTH = BITS / 8;
    uint8_t data[WIDTH];
public:
    base_blob()
    {
        memset(data, 0, sizeof(data));
    }

    explicit base_blob(const std::vector<unsigned char>& vch);

    bool IsNull() const
    {
        for (int i = 0; i < WIDTH; i++)
            if (data[i] != 0)
                return false;
        return true;
    }

    void SetNull()
    {
        memset(data, 0, sizeof(data));
    }

    inline int Compare(const base_blob& other) const { return memcmp(data, other.data, sizeof(data)); }

    friend inline bool operator==(const base_blob& a, const base_blob& b) { return a.Compare(b) == 0; }
    friend inline bool operator!=(const base_blob& a, const base_blob& b) { return a.Compare(b) != 0; }
    friend inline bool operator<(const base_blob& a, const base_blob& b) { return a.Compare(b) < 0; }

    std::string GetHex() const;
    void SetHex(const char* psz);
    void SetHex(const std::string& str);
    std::string ToString() const;

    unsigned char* begin()
    {
        return &data[0];
    }

    unsigned char* end()
    {
        return &data[WIDTH];
    }

    const unsigned char* begin() const
    {
        return &data[0];
    }

    const unsigned char* end() const
    {
        return &data[WIDTH];
    }

    unsigned int size() const
    {
        return sizeof(data);
    }

    uint64_t GetUint64(int pos) const
    {
        const uint8_t* ptr = data + pos * 8;
        return ((uint64_t)ptr[0]) | \
               ((uint64_t)ptr[1]) << 8 | \
               ((uint64_t)ptr[2]) << 16 | \
               ((uint64_t)ptr[3]) << 24 | \
               ((uint64_t)ptr[4]) << 32 | \
               ((uint64_t)ptr[5]) << 40 | \
               ((uint64_t)ptr[6]) << 48 | \
               ((uint64_t)ptr[7]) << 56;
    }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        s.write((char*)data, sizeof(data));
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        s.read((char*)data, sizeof(data));
    }
};

/** 160-bit opaque blob.
 * @note This type is called uint160 for historical reasons only. It is an opaque
 * blob of 160 bits and has no integer operations.
 */
class n_uint160 : public base_blob<160> {
public:
    n_uint160() {}
    explicit n_uint160(const std::vector<unsigned char>& vch) : base_blob<160>(vch) {}
};

/** 256-bit opaque blob.
 * @note This type is called n_uint256 for historical reasons only. It is an
 * opaque blob of 256 bits and has no integer operations. Use arith_uint256 if
 * those are required.
 */
class n_uint256 : public base_blob<256> {
public:
    n_uint256() {}
    explicit n_uint256(const std::vector<unsigned char>& vch) : base_blob<256>(vch) {}

    /** A cheap hash function that just returns 64 bits from the result, it can be
     * used when the contents are considered uniformly random. It is not appropriate
     * when the value can easily be influenced from outside as e.g. a network adversary could
     * provide values to trigger worst-case behavior.
     */
    uint64_t GetCheapHash() const
    {
        return ReadLE64(data);
    }
};

/* n_uint256 from const char *.
 * This is a separate function because the constructor n_uint256(const char*) can result
 * in dangerously catching n_uint256(0).
 */
inline n_uint256 uint256S(const char *str)
{
    n_uint256 rv;
    rv.SetHex(str);
    return rv;
}
/* n_uint256 from std::string.
 * This is a separate function because the constructor n_uint256(const std::string &str) can result
 * in dangerously catching n_uint256(0) via std::string(const char*).
 */
inline n_uint256 uint256S(const std::string& str)
{
    n_uint256 rv;
    rv.SetHex(str);
    return rv;
}


class uint_error : public std::runtime_error {
public:
    explicit uint_error(const std::string& str) : std::runtime_error(str) {}
};

/** Template base class for unsigned big integers. */
template<unsigned int BITS>
class n_base_uint
{
protected:
    static constexpr int WIDTH = BITS / 32;
    uint32_t pn[WIDTH];
public:

    n_base_uint()
    {
        static_assert(BITS/32 > 0 && BITS%32 == 0, "Template parameter BITS must be a positive multiple of 32.");

        for (int i = 0; i < WIDTH; i++)
            pn[i] = 0;
    }

    n_base_uint(const n_base_uint& b)
    {
        static_assert(BITS/32 > 0 && BITS%32 == 0, "Template parameter BITS must be a positive multiple of 32.");

        for (int i = 0; i < WIDTH; i++)
            pn[i] = b.pn[i];
    }

    n_base_uint& operator=(const n_base_uint& b)
    {
        for (int i = 0; i < WIDTH; i++)
            pn[i] = b.pn[i];
        return *this;
    }

    n_base_uint(uint64_t b)
    {
        static_assert(BITS/32 > 0 && BITS%32 == 0, "Template parameter BITS must be a positive multiple of 32.");

        pn[0] = (unsigned int)b;
        pn[1] = (unsigned int)(b >> 32);
        for (int i = 2; i < WIDTH; i++)
            pn[i] = 0;
    }

    explicit n_base_uint(const std::string& str);

    bool operator!() const
    {
        for (int i = 0; i < WIDTH; i++)
            if (pn[i] != 0)
                return false;
        return true;
    }

    const n_base_uint operator~() const
    {
        n_base_uint ret;
        for (int i = 0; i < WIDTH; i++)
            ret.pn[i] = ~pn[i];
        return ret;
    }

    const n_base_uint operator-() const
    {
        n_base_uint ret;
        for (int i = 0; i < WIDTH; i++)
            ret.pn[i] = ~pn[i];
        ret++;
        return ret;
    }

    double getdouble() const;

    n_base_uint& operator=(uint64_t b)
    {
        pn[0] = (unsigned int)b;
        pn[1] = (unsigned int)(b >> 32);
        for (int i = 2; i < WIDTH; i++)
            pn[i] = 0;
        return *this;
    }

    n_base_uint& operator^=(const n_base_uint& b)
    {
        for (int i = 0; i < WIDTH; i++)
            pn[i] ^= b.pn[i];
        return *this;
    }

    n_base_uint& operator&=(const n_base_uint& b)
    {
        for (int i = 0; i < WIDTH; i++)
            pn[i] &= b.pn[i];
        return *this;
    }

    n_base_uint& operator|=(const n_base_uint& b)
    {
        for (int i = 0; i < WIDTH; i++)
            pn[i] |= b.pn[i];
        return *this;
    }

    n_base_uint& operator^=(uint64_t b)
    {
        pn[0] ^= (unsigned int)b;
        pn[1] ^= (unsigned int)(b >> 32);
        return *this;
    }

    n_base_uint& operator|=(uint64_t b)
    {
        pn[0] |= (unsigned int)b;
        pn[1] |= (unsigned int)(b >> 32);
        return *this;
    }

    n_base_uint& operator<<=(unsigned int shift);
    n_base_uint& operator>>=(unsigned int shift);

    n_base_uint& operator+=(const n_base_uint& b)
    {
        uint64_t carry = 0;
        for (int i = 0; i < WIDTH; i++)
        {
            uint64_t n = carry + pn[i] + b.pn[i];
            pn[i] = n & 0xffffffff;
            carry = n >> 32;
        }
        return *this;
    }

    n_base_uint& operator-=(const n_base_uint& b)
    {
        *this += -b;
        return *this;
    }

    n_base_uint& operator+=(uint64_t b64)
    {
        n_base_uint b;
        b = b64;
        *this += b;
        return *this;
    }

    n_base_uint& operator-=(uint64_t b64)
    {
        n_base_uint b;
        b = b64;
        *this += -b;
        return *this;
    }

    n_base_uint& operator*=(uint32_t b32);
    n_base_uint& operator*=(const n_base_uint& b);
    n_base_uint& operator/=(const n_base_uint& b);

    n_base_uint& operator++()
    {
        // prefix operator
        int i = 0;
        while (i < WIDTH && ++pn[i] == 0)
            i++;
        return *this;
    }

    const n_base_uint operator++(int)
    {
        // postfix operator
        const n_base_uint ret = *this;
        ++(*this);
        return ret;
    }

    n_base_uint& operator--()
    {
        // prefix operator
        int i = 0;
        while (i < WIDTH && --pn[i] == (uint32_t)-1)
            i++;
        return *this;
    }

    const n_base_uint operator--(int)
    {
        // postfix operator
        const n_base_uint ret = *this;
        --(*this);
        return ret;
    }

    int CompareTo(const n_base_uint& b) const;
    bool EqualTo(uint64_t b) const;

    friend inline const n_base_uint operator+(const n_base_uint& a, const n_base_uint& b) { return n_base_uint(a) += b; }
    friend inline const n_base_uint operator-(const n_base_uint& a, const n_base_uint& b) { return n_base_uint(a) -= b; }
    friend inline const n_base_uint operator*(const n_base_uint& a, const n_base_uint& b) { return n_base_uint(a) *= b; }
    friend inline const n_base_uint operator/(const n_base_uint& a, const n_base_uint& b) { return n_base_uint(a) /= b; }
    friend inline const n_base_uint operator|(const n_base_uint& a, const n_base_uint& b) { return n_base_uint(a) |= b; }
    friend inline const n_base_uint operator&(const n_base_uint& a, const n_base_uint& b) { return n_base_uint(a) &= b; }
    friend inline const n_base_uint operator^(const n_base_uint& a, const n_base_uint& b) { return n_base_uint(a) ^= b; }
    friend inline const n_base_uint operator>>(const n_base_uint& a, int shift) { return n_base_uint(a) >>= shift; }
    friend inline const n_base_uint operator<<(const n_base_uint& a, int shift) { return n_base_uint(a) <<= shift; }
    friend inline const n_base_uint operator*(const n_base_uint& a, uint32_t b) { return n_base_uint(a) *= b; }
    friend inline bool operator==(const n_base_uint& a, const n_base_uint& b) { return memcmp(a.pn, b.pn, sizeof(a.pn)) == 0; }
    friend inline bool operator!=(const n_base_uint& a, const n_base_uint& b) { return memcmp(a.pn, b.pn, sizeof(a.pn)) != 0; }
    friend inline bool operator>(const n_base_uint& a, const n_base_uint& b) { return a.CompareTo(b) > 0; }
    friend inline bool operator<(const n_base_uint& a, const n_base_uint& b) { return a.CompareTo(b) < 0; }
    friend inline bool operator>=(const n_base_uint& a, const n_base_uint& b) { return a.CompareTo(b) >= 0; }
    friend inline bool operator<=(const n_base_uint& a, const n_base_uint& b) { return a.CompareTo(b) <= 0; }
    friend inline bool operator==(const n_base_uint& a, uint64_t b) { return a.EqualTo(b); }
    friend inline bool operator!=(const n_base_uint& a, uint64_t b) { return !a.EqualTo(b); }

    std::string GetHex() const;
    void SetHex(const char* psz);
    void SetHex(const std::string& str);
    std::string ToString() const;

    unsigned int size() const
    {
        return sizeof(pn);
    }

    /**
     * Returns the position of the highest bit set plus one, or zero if the
     * value is zero.
     */
    unsigned int bits() const;

    uint64_t GetLow64() const
    {
        static_assert(WIDTH >= 2, "Assertion WIDTH >= 2 failed (WIDTH = BITS / 32). BITS is a template parameter.");
        return pn[0] | (uint64_t)pn[1] << 32;
    }
};

/** 256-bit unsigned big integer. */
class arith_uint256 : public n_base_uint<256> {
public:
    arith_uint256() {}
    arith_uint256(const n_base_uint<256>& b) : n_base_uint<256>(b) {}
    arith_uint256(uint64_t b) : n_base_uint<256>(b) {}
    explicit arith_uint256(const std::string& str) : n_base_uint<256>(str) {}

    IMPLEMENT_SERIALIZE
    (
        READWRITE(pn);
    )

    /**
     * The "compact" format is a representation of a whole
     * number N using an unsigned 32bit number similar to a
     * floating point format.
     * The most significant 8 bits are the unsigned exponent of base 256.
     * This exponent can be thought of as "number of bytes of N".
     * The lower 23 bits are the mantissa.
     * Bit number 24 (0x800000) represents the sign of N.
     * N = (-1^sign) * mantissa * 256^(exponent-3)
     *
     * Satoshi's original implementation used BN_bn2mpi() and BN_mpi2bn().
     * MPI uses the most significant bit of the first byte as sign.
     * Thus 0x1234560000 is compact (0x05123456)
     * and  0xc0de000000 is compact (0x0600c0de)
     *
     * Bitcoin only uses this "compact" format for encoding difficulty
     * targets, which are unsigned 256bit quantities.  Thus, all the
     * complexities of the sign bit and using base 256 are probably an
     * implementation accident.
     */
    arith_uint256& SetCompact(uint32_t nCompact, bool *pfNegative = nullptr, bool *pfOverflow = nullptr);
    uint32_t GetCompact(bool fNegative = false) const;
    friend n_uint256 ArithToUint256(const arith_uint256 &);
    friend arith_uint256 UintToArith256(const n_uint256 &);
};

n_uint256 ArithToUint256(const arith_uint256 &);
arith_uint256 UintToArith256(const n_uint256 &);

#endif // BITCOIN_ARITH_UINT256_H
