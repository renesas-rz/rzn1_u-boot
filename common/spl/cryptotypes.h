/*
 * cryptotypes.h
 *
 *  Created on: 5 Feb 2015
 *	  Author: goveyd
 */

#ifndef CRYPTOTYPES_H_
#define CRYPTOTYPES_H_

/******************************************************************************
 * Includes
 ******************************************************************************/
/******************************************************************************
 * Defines and Typedefs
 *********************************************************************************/

/* ecdsa_verify error codes - values now defined in lces_errno.h */
typedef enum {
	ECDSA_VERIFY_STATUS_VERIFIED = 0,
} ecdsa_verify_return_t;

/* BLp Header format structure */
typedef struct {
	uint32_t Type; /** Type. */
	uint32_t PubKeyType; /** Type of public key */

	/** Signature. */
	uint8_t r[32]; /** r. */
	uint8_t s[32]; /** s. */

	/** Public key (if included in image). */
	uint8_t Qx[32]; /** Qx. */
	uint8_t Qy[32]; /** Qy. */

	uint32_t EncryptionKey[10]; /** Key. */
	uint32_t EncryptionIV[4];   /** IV. */
	uint32_t ImageLen;          /** Image length. */
	/** Image attributes. */
	uint32_t Header_version_attribute_type_ID;
	uint32_t Header_version_attribute_value;
	uint32_t Custom_attribute_Load_Address_type_ID;
	/* Big Endian Custom_attribute_Load_Address_value needs Endian swap */
	uint32_t Custom_attribute_Load_Address_value_Big_Endian;
	uint32_t Custom_attribute_Execution_Offset_type_ID;
	/* Big Endian Custom_attribute_Execution_Offset_value needs Endian swap */
	uint32_t Custom_attribute_Execution_Offset_value_Big_Endian;
	uint32_t Unused_attribute3_type_ID;
	uint32_t Unused_attribute3_value;
	uint32_t Unused_attribute4_type_ID;
	uint32_t Unused_attribute4_value;
	uint32_t Unused_attribute5_type_ID;
	uint32_t Unused_attribute5_value;
	uint32_t Unused_attribute6_type_ID;
	uint32_t Unused_attribute6_value;
	uint32_t Unused_attribute7_type_ID;
	uint32_t Unused_attribute7_value;

	/** Certificate count. */
	uint32_t CertificateCount;
} BLpHeader_t;

typedef struct {
	union {
		uint32_t data_32_bitAccess[512]; /** For Alignment and 32 bit access. */
		uint8_t data_8_bitAccess[2048];  /** Size. */
	} Union;
} SB_StorageArea_t;

typedef struct {
	union {
		uint32_t KeyHash_32_bitAccess[8]; /** For Alignment and 32 bit access. */
		uint8_t KeyHash_8_bitAccess[32];  /** Size. */
	} Union;
} KeyHash_t;

#endif /* CRYPTOTYPES_H_ */
