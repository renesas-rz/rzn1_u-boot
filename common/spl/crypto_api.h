#ifndef _CRYPTOTYPES_H_INCLUDED_
#define _CRYPTOTYPES_H_INCLUDED_
#include "cryptotypes.h"

/* The address of the Crypto API structure */
#define CRYPTO_API_ADDRESS (0x00000060)

/* API version MMMM.mmmm v1.04 */
#define CRYPTO_API_VERSION (0x00010004)

typedef enum {
	IMAGE_SOURCE_SERIAL_FLASH,  /* 2nd stage Bootloader to be loaded from QSPI serial flash */
	IMAGE_SOURCE_NAND_FLASH,    /* 2nd stage Bootloader to be loaded from NAND flash */
	IMAGE_SOURCE_USB,           /* 2nd stage Bootloader to be loaded from USB */
	IMAGE_SOURCE_NONE
} IMAGE_SOURCE_TYPE_E;

/* Example of API usage:
 *
 *      #include "crypto_api.h"
 *
 *      boot_rom_api_t* pAPI = (boot_rom_api_t*)CRYPTO_API_ADDRESS;
 *
 *      if ( ESUCCESS != pAPI->crypto_init() ||
 *              ECDSA_VERIFY_STATUS_VERIFIED != pAPI->ecdsa_verify(pBLp_Header,
 *                                                            pPayload_Data,
 *                                                            Execution_offset,
 *                                                            pStorageArea,
 *                                                            pHash)}
 *      {
 *              SignatureVerifyFailed_handler();
 *      }
 */

/* Structure for accessing the Crypto API:
 * CRYPTO_API_ADDRESS + 0x00: CRYPTO_API_VERSION
 * CRYPTO_API_ADDRESS + 0x04: Address of crypto_init function
 * CRYPTO_API_ADDRESS + 0x08: Address of ecdsa_verify function
 * CRYPTO_API_ADDRESS + 0x0c: Address of read_security_state function
 * CRYPTO_API_ADDRESS + 0x10: Indicator of Boot source (NAND, QSPI or USB)
 * CRYPTO_API_ADDRESS + 0x14: Index of used Image
 * CRYPTO_API_ADDRESS + 0x18: Index of used Header
 * CRYPTO_API_ADDRESS + 0x1c: ROM address of start of load data
 * CRYPTO_API_ADDRESS + 0x20: ROM address of end of load data
 * CRYPTO_API_ADDRESS + 0x24: RAM address of start of .data section
 * CRYPTO_API_ADDRESS + 0x28: RAM address of end of .data section
 * CRYPTO_API_ADDRESS + 0x2c: RAM address of start of .bss section
 * CRYPTO_API_ADDRESS + 0x30: RAM address of end of .bss section
 * CRYPTO_API_ADDRESS + 0x34: ROM address of start of crypto library
 * CRYPTO_API_ADDRESS + 0x38: ROM address of end of crypto library
 * CRYPTO_API_ADDRESS + 0x3c: RAM address of the start of the stack
 */
typedef struct boot_rom_api_s {
	/* Crypto API version */
	uint32_t APIversion;

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

	/**************************************************************************
	 *
	 *  Function:       read_security_state
	 *
	 *  Return value:   bit 0  indicates that secure boot is required
	 *                  bit 1  indicates that crypto hardware is disabled
	 *
	 *                  0x00    Does not require secure boot,
	 *                          crypto enabled.
	 *                  0x01    Requires secure boot,
	 *                          crypto enabled
	 *                  0x02    Does not require secure boot,
	 *                          crypto disabled
	 *                  0x03    Requires secure boot,
	 *                          but crypto disabled
	 *
	 *  Parameters:     none
	 *  Description:    Reads the state of the security and crypto bits
	 *************************************************************************/
	uint32_t (*read_security_state)(void);

	/* Boot Type:
	 * The Boot ROM Code will populate this with a value
	 * to indicate the boot type */
	IMAGE_SOURCE_TYPE_E *pBoot_Type;

	/* Index of found image (if applicable) */
	uint32_t *pImageIndex;

	/* SPKG Header index used */
	uint32_t *pHeaderIndex;

	/* __load_data_start__ */
	void *pload_data_start;

	/* __load_data_end__ */
	void *pload_data_end;

	/* __data_start__ */
	void *pdata_start;

	/* __data_end__ */
	void *pdata_end;

	/* __bss_start__ */
	void *pbss_start;

	/* __bss_end__ */
	void *pbss_end;

	/* Crypto library start */
	void *pcrypto_start;

	/* Crypto library end */
	void *pcrypto_end;

	/* Stack Start */
	void *pstack_start;

} boot_rom_api_t;
#endif /* _CRYPTOTYPES_H_INCLUDED_ */
