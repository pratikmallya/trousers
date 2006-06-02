
/*
 * Licensed Materials - Property of IBM
 *
 * trousers - An open source TCG Software Stack
 *
 * (C) Copyright International Business Machines Corp. 2005
 *
 */


#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

#include "trousers/tss.h"
#include "trousers/trousers.h"
#include "spi_internal_types.h"
#include "spi_utils.h"
#include "capabilities.h"
#include "tsplog.h"
#include "obj.h"

TSS_RESULT
obj_pcrs_add(TSS_HCONTEXT tspContext, TSS_HOBJECT *phObject)
{
	TSS_RESULT result;
	struct tr_pcrs_obj *pcrs = calloc(1, sizeof(struct tr_pcrs_obj));

	if (pcrs == NULL) {
		LogError("malloc of %zd bytes failed.",
				sizeof(struct tr_pcrs_obj));
		return TSPERR(TSS_E_OUTOFMEMORY);
	}

	if ((result = obj_list_add(&pcrs_list, tspContext, 0, pcrs, phObject))) {
		free(pcrs);
		return result;
	}

	return TSS_SUCCESS;
}

void
free_pcrs(struct tr_pcrs_obj *pcrs)
{
	free(pcrs->select.pcrSelect);
	free(pcrs);
}

TSS_RESULT
obj_pcrs_remove(TSS_HOBJECT hObject, TSS_HCONTEXT tspContext)
{
        struct tsp_object *obj, *prev = NULL;
	struct obj_list *list = &pcrs_list;
        TSS_RESULT result = TSPERR(TSS_E_INVALID_HANDLE);

        pthread_mutex_lock(&pcrs_list.lock);

        for (obj = list->head; obj; prev = obj, obj = obj->next) {
                if (obj->handle == hObject) {
			/* validate tspContext */
			if (obj->tspContext != tspContext)
				break;

                        free_pcrs(obj->data);
                        if (prev)
                                prev->next = obj->next;
                        else
                                list->head = obj->next;
                        free(obj);
                        result = TSS_SUCCESS;
                        break;
                }
        }

        pthread_mutex_unlock(&pcrs_list.lock);

        return result;

}

TSS_BOOL
obj_is_pcrs(TSS_HOBJECT hObject)
{
	TSS_BOOL answer = FALSE;

	if ((obj_list_get_obj(&pcrs_list, hObject))) {
		answer = TRUE;
		obj_list_put(&pcrs_list);
	}

	return answer;
}

TSS_RESULT
obj_pcrs_get_tsp_context(TSS_HTPM hPcrs, TSS_HCONTEXT *tspContext)
{
	struct tsp_object *obj;

	if ((obj = obj_list_get_obj(&pcrs_list, hPcrs)) == NULL)
		return TSPERR(TSS_E_INVALID_HANDLE);

	*tspContext = obj->tspContext;

	obj_list_put(&pcrs_list);

	return TSS_SUCCESS;
}

TSS_RESULT
obj_pcrs_get_selection(TSS_HPCRS hPcrs, TCPA_PCR_SELECTION *pcrSelect)
{
	struct tsp_object *obj;
	struct tr_pcrs_obj *pcrs;
	TSS_RESULT result = TSS_SUCCESS;

	if ((obj = obj_list_get_obj(&pcrs_list, hPcrs)) == NULL)
		return TSPERR(TSS_E_INVALID_HANDLE);

	pcrs = (struct tr_pcrs_obj *)obj->data;

	if (pcrs->select.pcrSelect == NULL) {
		memcpy(pcrSelect, &pcrs->select, sizeof(TCPA_PCR_SELECTION));
	} else {
		if ((pcrSelect->pcrSelect = calloc(1,
					pcrs->select.sizeOfSelect)) == NULL) {
			LogError("malloc of %d bytes failed.",
					pcrs->select.sizeOfSelect);
			result = TSPERR(TSS_E_OUTOFMEMORY);
			goto done;
		}
		pcrSelect->sizeOfSelect = pcrs->select.sizeOfSelect;
		memcpy(pcrSelect->pcrSelect, pcrs->select.pcrSelect,
				pcrs->select.sizeOfSelect);
	}

done:
	obj_list_put(&pcrs_list);

	return result;
}

TSS_RESULT
obj_pcrs_set_values(TSS_HPCRS hPcrs, TCPA_PCR_COMPOSITE *pcrComp)
{
	TSS_RESULT result = TSS_SUCCESS;
	TCPA_PCR_SELECTION *select = &(pcrComp->select);
	UINT16 i, val_idx = 0;

	for (i = 0; i < select->sizeOfSelect * 8; i++) {
		if (select->pcrSelect[i / 8] & (1 << (i % 8))) {
			if ((result = obj_pcrs_set_value(hPcrs, i, TCPA_SHA1_160_HASH_LEN,
							 (BYTE *)&pcrComp->pcrValue[val_idx])))
				break;

			val_idx++;
		}
	}

	return result;
}

TSS_RESULT
obj_pcrs_set_value(TSS_HPCRS hPcrs, UINT32 idx, UINT32 size, BYTE *value)
{
	struct tsp_object *obj;
	struct tr_pcrs_obj *pcrs;
	TSS_RESULT result = TSS_SUCCESS;
	UINT16 bytes_to_hold = (idx / 8) + 1;

	if ((obj = obj_list_get_obj(&pcrs_list, hPcrs)) == NULL)
		return TSPERR(TSS_E_INVALID_HANDLE);

	pcrs = (struct tr_pcrs_obj *)obj->data;

	/* allocate the selection structure */
	if (pcrs->select.pcrSelect == NULL) {
		if ((pcrs->select.pcrSelect = malloc(bytes_to_hold)) == NULL) {
			LogError("malloc of %d bytes failed.", bytes_to_hold);
			result = TSPERR(TSS_E_OUTOFMEMORY);
			goto done;
		}
		pcrs->select.sizeOfSelect = bytes_to_hold;
		memset(pcrs->select.pcrSelect, 0, bytes_to_hold);

		/* allocate the pcr array */
		if ((pcrs->pcrs = malloc(bytes_to_hold * 8 *
					 TCPA_SHA1_160_HASH_LEN)) == NULL) {
			LogError("malloc of %d bytes failed.",
				bytes_to_hold * 8 * TCPA_SHA1_160_HASH_LEN);
			result = TSPERR(TSS_E_OUTOFMEMORY);
			goto done;
		}
	} else if (pcrs->select.sizeOfSelect < bytes_to_hold) {
		if ((pcrs->select.pcrSelect = realloc(pcrs->select.pcrSelect,
				bytes_to_hold)) == NULL) {
			LogError("malloc of %d bytes failed.", bytes_to_hold);
			result = TSPERR(TSS_E_OUTOFMEMORY);
			goto done;
		}
		/* set the newly allocated bytes to 0 */
		memset(&pcrs->select.pcrSelect[pcrs->select.sizeOfSelect], 0,
				bytes_to_hold - pcrs->select.sizeOfSelect);
		pcrs->select.sizeOfSelect = bytes_to_hold;

		/* realloc the pcrs array */
		if ((pcrs->pcrs = realloc(pcrs->pcrs, bytes_to_hold * 8 *
						sizeof(TCPA_PCRVALUE))) == NULL) {
			LogError("malloc of %d bytes failed.",
					bytes_to_hold * 8 * TCPA_SHA1_160_HASH_LEN);
			result = TSPERR(TSS_E_OUTOFMEMORY);
			goto done;
		}
	}

	/* set the bit in the selection structure */
	pcrs->select.pcrSelect[idx / 8] |= (1 << (idx % 8));

	/* set the value in the pcrs array */
	memcpy(&(pcrs->pcrs[idx]), value, size);
done:
	obj_list_put(&pcrs_list);

	return result;
}

TSS_RESULT
obj_pcrs_get_value(TSS_HPCRS hPcrs, UINT32 idx, UINT32 *size, BYTE **value)
{
	struct tsp_object *obj;
	struct tr_pcrs_obj *pcrs;
	TSS_RESULT result = TSS_SUCCESS;

	if ((obj = obj_list_get_obj(&pcrs_list, hPcrs)) == NULL)
		return TSPERR(TSS_E_INVALID_HANDLE);

	pcrs = (struct tr_pcrs_obj *)obj->data;

	if (pcrs->select.sizeOfSelect < (idx / 8) + 1) {
		result = TSPERR(TSS_E_BAD_PARAMETER);
		goto done;
	}

	if ((*value = calloc_tspi(obj->tspContext, TCPA_SHA1_160_HASH_LEN))
					== NULL) {
		LogError("malloc of %d bytes failed.", TCPA_SHA1_160_HASH_LEN);
		result = TSPERR(TSS_E_OUTOFMEMORY);
		goto done;
	}

	*size = TCPA_SHA1_160_HASH_LEN;
	memcpy(*value, &pcrs->pcrs[idx], TCPA_SHA1_160_HASH_LEN);

done:
	obj_list_put(&pcrs_list);

	return result;
}

/* This should only be called through paths with a verified connected
 * TCS context */
TSS_RESULT
obj_pcrs_get_composite(TSS_HPCRS hPcrs, TCPA_PCRVALUE *comp)
{
	struct tsp_object *obj;
	struct tr_pcrs_obj *pcrs;
	TSS_RESULT result = TSS_SUCCESS;
	UINT16 num_pcrs, bytes_to_hold;

	if ((obj = obj_list_get_obj(&pcrs_list, hPcrs)) == NULL)
		return TSPERR(TSS_E_INVALID_HANDLE);

	pcrs = (struct tr_pcrs_obj *)obj->data;

	if ((num_pcrs = get_num_pcrs(obj->tcsContext)) == 0) {
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		goto done;
	}
	bytes_to_hold = num_pcrs / 8;

	/* Is the current select object going to be interpretable by the TPM?
	 * If the select object is of a size greater than the one the TPM
	 * wants, just calculate the composite hash and let the TPM return an
	 * error code to the user.  If its less than the size of the one the
	 * TPM wants, add extra zero bytes until its the right size. */
	if (bytes_to_hold > pcrs->select.sizeOfSelect) {
		if ((pcrs->select.pcrSelect = realloc(pcrs->select.pcrSelect,
						bytes_to_hold)) == NULL) {
			LogError("malloc of %d bytes failed.", bytes_to_hold);
			result = TSPERR(TSS_E_OUTOFMEMORY);
			goto done;
		}
		/* set the newly allocated bytes to 0 */
		memset(&pcrs->select.pcrSelect[pcrs->select.sizeOfSelect], 0,
				bytes_to_hold - pcrs->select.sizeOfSelect);
		pcrs->select.sizeOfSelect = bytes_to_hold;

		/* realloc the pcr array as well */
		if ((pcrs->pcrs = realloc(pcrs->pcrs,
			(bytes_to_hold * 8) * TCPA_SHA1_160_HASH_LEN))
								== NULL) {
			LogError("malloc of %d bytes failed.",
				 (bytes_to_hold * 8) * TCPA_SHA1_160_HASH_LEN);
			result = TSPERR(TSS_E_OUTOFMEMORY);
			goto done;
		}
	}

#ifdef TSS_DEBUG
	{
		int i;
		for (i = 0; i < pcrs->select.sizeOfSelect * 8; i++) {
			if (pcrs->select.pcrSelect[i/8] & (1 << (i % 8))) {
				LogDebug("PCR%d: Selected", i);
				LogBlobData(APPID, TCPA_SHA1_160_HASH_LEN,
					    (unsigned char *)&pcrs->pcrs[i]);
			} else {
				LogDebug("PCR%d: Not Selected", i);
			}
		}
	}
#endif

	result = calc_composite_from_object(&pcrs->select, pcrs->pcrs, comp);

done:
	obj_list_put(&pcrs_list);

	return result;
}

TSS_RESULT
obj_pcrs_select_index(TSS_HPCRS hPcrs, UINT32 idx)
{
	struct tsp_object *obj;
	struct tr_pcrs_obj *pcrs;
	TSS_RESULT result = TSS_SUCCESS;
	UINT16 bytes_to_hold = (idx / 8) + 1;

	if ((obj = obj_list_get_obj(&pcrs_list, hPcrs)) == NULL)
		return TSPERR(TSS_E_INVALID_HANDLE);

	pcrs = (struct tr_pcrs_obj *)obj->data;

	/* allocate the selection structure */
	if (pcrs->select.pcrSelect == NULL) {
		if ((pcrs->select.pcrSelect = malloc(bytes_to_hold)) == NULL) {
			LogError("malloc of %d bytes failed.", bytes_to_hold);
			result = TSPERR(TSS_E_OUTOFMEMORY);
			goto done;
		}
		pcrs->select.sizeOfSelect = bytes_to_hold;
		memset(pcrs->select.pcrSelect, 0, bytes_to_hold);

		/* alloc the pcrs array */
		if ((pcrs->pcrs = malloc(bytes_to_hold * 8 *
						TCPA_SHA1_160_HASH_LEN)) == NULL) {
			LogError("malloc of %d bytes failed.", bytes_to_hold *
					8 * TCPA_SHA1_160_HASH_LEN);
			result = TSPERR(TSS_E_OUTOFMEMORY);
			goto done;
		}
	} else if (pcrs->select.sizeOfSelect < bytes_to_hold) {
		if ((pcrs->select.pcrSelect = realloc(pcrs->select.pcrSelect,
				bytes_to_hold)) == NULL) {
			LogError("malloc of %d bytes failed.", bytes_to_hold);
			result = TSPERR(TSS_E_OUTOFMEMORY);
			goto done;
		}
		/* set the newly allocated bytes to 0 */
		memset(&pcrs->select.pcrSelect[pcrs->select.sizeOfSelect], 0,
				bytes_to_hold - pcrs->select.sizeOfSelect);
		pcrs->select.sizeOfSelect = bytes_to_hold;

		/* realloc the pcrs array */
		if ((pcrs->pcrs = realloc(pcrs->pcrs, bytes_to_hold * 8 *
						TCPA_SHA1_160_HASH_LEN)) == NULL) {
			LogError("malloc of %d bytes failed.", bytes_to_hold *
					8 * TCPA_SHA1_160_HASH_LEN);
			result = TSPERR(TSS_E_OUTOFMEMORY);
			goto done;
		}
	}

	/* set the bit in the selection structure */
	pcrs->select.pcrSelect[idx / 8] |= (1 << (idx % 8));

done:
	obj_list_put(&pcrs_list);

	return result;
}

TSS_RESULT
calc_composite_from_object(TCPA_PCR_SELECTION *select, TCPA_PCRVALUE * arrayOfPcrs, TCPA_DIGEST * digestOut)
{
	UINT32 size, index;
	BYTE mask;
	BYTE hashBlob[1024];
	UINT32 numPCRs = 0;
	UINT16 offset = 0;
	UINT16 sizeOffset = 0;

	if (select->sizeOfSelect > 0) {
		sizeOffset = 0;
		Trspi_LoadBlob_PCR_SELECTION(&sizeOffset, hashBlob, select);
		offset = sizeOffset + 4;

		for (size = 0; size < select->sizeOfSelect; size++) {
			for (index = 0, mask = 1; index < 8; index++, mask = mask << 1) {
				if (select->pcrSelect[size] & mask) {
					memcpy(&hashBlob[(numPCRs * TCPA_SHA1_160_HASH_LEN) + offset],
					       arrayOfPcrs[index + (size << 3)].digest,
					       TCPA_SHA1_160_HASH_LEN);
					numPCRs++;
				}
			}
		}

		if (numPCRs > 0) {
			offset += (numPCRs * TCPA_SHA1_160_HASH_LEN);
			UINT32ToArray(numPCRs * TCPA_SHA1_160_HASH_LEN, &hashBlob[sizeOffset]);

			Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, digestOut->digest);

			return TSS_SUCCESS;
		}
	}

	return TSPERR(TSS_E_INTERNAL_ERROR);
}


