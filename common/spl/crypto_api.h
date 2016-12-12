#ifndef _CRYPTOTYPES_H_INCLUDED_
#define _CRYPTOTYPES_H_INCLUDED_
#include "cryptotypes.h"

/* The address of the Crypto API structure */
#define CRYPTO_API_ADDRESS (0x00000060)

typedef struct secure_boot_api_s {

	uint32_t reserved;

	/* Initialise the crypto block
	 * Returns 0 for success.
	 */
	/**************************************************************************
	 *
	 *  Function:       crypto_init
	 *
	 *  Return value:   (uint32_t type)
	 *                                      ESUCCESS (0) for success,
	 *                                      or non-zero error code for failure
	 *************************************************************************/
	uint32_t (*crypto_init)(void);

	/**************************************************************************
	 *
	 *  Function:       ecdsa_verify
	 *
	 *  Return value:   (ecdsa_verify_return_t type)
	 *                  ECDSA_VERIFY_STATUS_VERIFIED (ESUCCESS, 0) for success,
	 *                  or non-zero error code for failure
	 *
	 *  Parameters:     pBLp_Header         Pointer to the SPKG Header
	 *                                      which cannot reside at address 0
	 *                  pPayload_Data       Pointer to the payload data. This
	 *                                      will be 264 bytes offset from the
	 *                                      BLp Header if they are contiguous.
	 *                  Execution_offset    Expected execution offset, to be
	 *                                      checked against the attribute
	 *                                      in the BLp Header.
	 *                  pStorageArea        Pointer to a 2KB RAM buffer for use
	 *                                      in signature verification.
	 *                  pKeyHash            Pointer to the hash of the public
	 *                                      key.
	 *
	 *  Description:    Publicly exposed function to verify the signature of a
	 *                  BLp image.
	 *                  The hash of the image is calculated and verified
	 *                  against the signature contained in the header, and the
	 *                  key hash is calculated and compared to the passed in
	 *                  value. Additionally, the execution offset in the BLp
	 *                  header is compared to the passed in value.
	 *************************************************************************/
	ecdsa_verify_return_t (*ecdsa_verify)(
			const BLpHeader_t * const pBLp_Header,
			const uint32_t * const pPayload_Data,
			const uint32_t Execution_offset,
			SB_StorageArea_t *pStorageArea,
			const KeyHash_t * const pHash);

} secure_boot_api_t;

#endif /* _CRYPTOTYPES_H_INCLUDED_ */
