
/*
 * Licensed Materials - Property of IBM
 *
 * trousers - An open source TCG Software Stack
 *
 * (C) Copyright International Business Machines Corp. 2004-2007
 *
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "trousers/tss.h"
#include "trousers/trousers.h"
#include "trousers_types.h"
#include "spi_utils.h"
#include "capabilities.h"
#include "tsplog.h"
#include "obj.h"


#ifdef TSS_BUILD_TRANSPORT
TSS_RESULT
Transport_ReadCurrentTicks(TSS_HCONTEXT tspContext,           /* in */
			   UINT32*      pulCurrentTime,       /* out */
			   BYTE**       prgbCurrentTime)      /* out */
{
	TSS_RESULT result;
	UINT32 decLen = 0;
	BYTE *dec = NULL;
	TCS_HANDLE handlesLen = 0;
	UINT64 offset;

	if ((result = obj_context_transport_init(tspContext)))
		return result;

	LogDebugFn("Executing in a transport session");

	if ((result = obj_context_transport_execute(tspContext, TPM_ORD_GetTicks, 0, NULL,
						    NULL, &handlesLen, NULL, NULL, NULL, &decLen,
						    &dec)))
		return result;

	offset = 0;
	Trspi_UnloadBlob_UINT32(&offset, pulCurrentTime, dec);

	if ((*prgbCurrentTime = malloc(*pulCurrentTime)) == NULL) {
		LogError("malloc of %u bytes failed", *pulCurrentTime);
		return TSPERR(TSS_E_OUTOFMEMORY);
	}
	Trspi_UnloadBlob(&offset, *pulCurrentTime, dec, *prgbCurrentTime);

	return TSS_SUCCESS;
}

TSS_RESULT
Transport_TickStampBlob(TSS_HCONTEXT   tspContext,            /* in */
			TCS_KEY_HANDLE hKey,                  /* in */
			TPM_NONCE*     antiReplay,            /* in */
			TPM_DIGEST*    digestToStamp,	      /* in */
			TPM_AUTH*      privAuth,              /* in, out */
			UINT32*        pulSignatureLength,    /* out */
			BYTE**         prgbSignature,	      /* out */
			UINT32*        pulTickCountLength,    /* out */
			BYTE**         prgbTickCount)	      /* out */
{
	TSS_RESULT result;
	UINT32 handlesLen, decLen = 0;
	TCS_HANDLE *handles;
	BYTE *dec = NULL;
	UINT64 offset;
	TPM_DIGEST pubKeyHash;
	BYTE data[sizeof(TPM_NONCE) + sizeof(TPM_DIGEST)];

	if ((result = obj_context_transport_init(tspContext)))
		return result;

	LogDebugFn("Executing in a transport session");

	offset = 0;
	Trspi_LoadBlob_NONCE(&offset, data, antiReplay);
	Trspi_LoadBlob_DIGEST(&offset, data, digestToStamp);

	if ((result = obj_tcskey_get_pubkeyhash(hKey, pubKeyHash.digest)))
		return result;

	handlesLen = 1;
	if ((handles = malloc(sizeof(TCS_HANDLE))) == NULL) {
		LogError("malloc of %zd bytes failed", sizeof(TCS_HANDLE));
		return TSPERR(TSS_E_OUTOFMEMORY);
	}

	*handles = hKey;
	if ((result = obj_context_transport_execute(tspContext, TPM_ORD_TickStampBlob, sizeof(data),
						    data, &pubKeyHash, &handlesLen, &handles,
						    privAuth, NULL, &decLen, &dec)))
		return result;

	offset = 0;
	Trspi_UnloadBlob_UINT32(&offset, pulSignatureLength, dec);
	if ((*prgbSignature = malloc(*pulSignatureLength)) == NULL) {
		free(handles);
		free(dec);
		LogError("malloc of %u bytes failed", *pulSignatureLength);
		return TSPERR(TSS_E_OUTOFMEMORY);
	}
	Trspi_UnloadBlob(&offset, *pulSignatureLength, dec, *prgbSignature);

	Trspi_UnloadBlob_UINT32(&offset, pulTickCountLength, dec);
	if ((*prgbTickCount = malloc(*pulTickCountLength)) == NULL) {
		free(*prgbSignature);
		free(handles);
		free(dec);
		LogError("malloc of %u bytes failed", *pulTickCountLength);
		return TSPERR(TSS_E_OUTOFMEMORY);
	}
	Trspi_UnloadBlob(&offset, *pulTickCountLength, dec, *prgbTickCount);

	free(handles);
	free(dec);

	return result;
}
#endif