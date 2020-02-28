
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "bn_misc.h"
#include "bn.h"
#include "bn_lcl.h"



void CRYPTO_lock(int mode, int type, const char *file, int line)
{
	fprintf(stderr, "CRYPTO_lock, damn it...\n");
}

void ERR_LCP_put_error(int lib, int func, int reason, const char *file, int line)
{
	fprintf(stderr, "ERR_LCP_put_error, damn it...\n");
}

void CRYPTO_THREADID_LCP_current(CRYPTO_THREADID_LCP *id)
{
	fprintf(stderr, "CRYPTO_THREADID_LCP_current, damn it...\n");
}

void ERR_LCP_clear_error()
{
	fprintf(stderr, "ERR_LCP_clear_error, damn it...\n");
}

unsigned long ERR_LCP_peek_last_error(void)
{
	fprintf(stderr, "ERR_LCP_peek_last_error, damn it...\n");
	return(0);
}

