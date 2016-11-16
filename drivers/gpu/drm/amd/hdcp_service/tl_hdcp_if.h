/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef TL_HDCP_IF_H_
#define TL_HDCP_IF_H_

typedef uint32_t tciCommandId_t;
typedef uint32_t tciResponseId_t;
typedef uint32_t tciReturnCode_t;

/**< Responses have bit 31 set */
#define RSP_ID_MASK (1U << 31)
#define RSP_ID(cmdId) (((uint32_t)(cmdId)) | RSP_ID_MASK)
#define IS_CMD(cmdId) ((((uint32_t)(cmdId)) & RSP_ID_MASK) == 0)
#define IS_RSP(cmdId) ((((uint32_t)(cmdId)) & RSP_ID_MASK) == RSP_ID_MASK)

/**
 * Return codes of Trustlet commands.
 */
#define RET_OK              0            /**< Set, if processing is error free */
#define RET_ERR_UNKNOWN_CMD 1            /**< Unknown command */
#define RET_CUSTOM_START    2

/**
 * TCI command header.
 */
typedef struct{
    tciCommandId_t commandId; /**< Command ID */
} tciCommandHeader_t;

/**
 * TCI response header.
 */
typedef struct{
    tciResponseId_t     responseId; /**< Response ID (must be command ID | RSP_ID_MASK )*/
    tciReturnCode_t     returnCode; /**< Return code of command */
} tciResponseHeader_t;

/*
 * Other unsorted stuff
 */
typedef enum _HDCP_CMD_ID
{
    HDCP_CMD_HOST_CMDS                = 0x1,    // Submit HDCP related commands
} HDCP_CMD_ID;

#define HDCP_SUCCESS                         0x00000000
#define HDCP_ERR_GENERIC_ERROR               0x00001001
#define HDCP_ERR_MEMORY                      0x00001002
#define HDCP_ERR_BUFFER_OVERFLOW             0x00001003
#define HDCP_ERR_INVALID_PARAMETER           0x00001004
#define HDCP_ERR_DATA_LENGTH                 0x00001005
#define HDCP_ERR_DATA_ALIGNMENT              0x00001006
#define HDCP_ERR_NULL_PTR                    0x00001007
#define HDCP_ERR_FUNCTION_NOT_SUPPORTED      0x00001008
#define HDCP_ERR_CANNOT_GET_SECURE_MEMORY    0x00001009
#define HDCP_ERR_INVALID_COMMAND_ID          0x0000100A
#define HDCP_ERR_INCORRECT_SEQ_NUM           0x0000100B
#define HDCP_ERR_TOO_MANY_SESSIONS           0x0000100C
#define HDCP_ERR_INVALID_TRANSMITTER_ID      0x0000100D
#define HDCP_ERR_PRNG_FAILED                 0x0000100E
#define HDCP_ERR_RECV_ID_IN_REVOCATION_LIST  0x0000100F
#define HDCP_ERR_SIGN_VERIFICATION_FAILED    0x00001010
#define HDCP_ERR_TIMER_OVER_LIMIT            0x00001011
#define HDCP_ERR_H_COMPARE_FAILED            0x00001012
#define HDCP_ERR_MAX_DEVS_EXCEEDED           0x00001013
#define HDCP_ERR_MAX_CASCADE_EXCEEDED        0x00001014
#define HDCP_ERR_L_COMPARE_FAILED            0x00001015
#define HDCP_ERR_LC_FAILED                   0x00001016
#define HDCP_ERR_V_COMPARE_FAILED            0x00001017
#define HDCP_ERR_CANNOT_LOCK_BUFFER          0x00001018
#define HDCP_ERR_INCORRECT_OMAC              0x00001019
#define HDCP_ERR_TIMER_NOT_RUNNING           0x0000101A
#define HDCP_ERR_SRM_ISSUE                   0x0000101B
#define HDCP_ERR_TIMESTAMP                   0x0000101C
#define HDCP_ERR_CANNOT_GET_STORGE_DATA      0x0000101D
#define HDCP_ERR_TIMER_EXPRIRED              0x0000101E
#define HDCP_ERR_D_KEY_FAILED                0x0000101F
#define HDCP_ERR_SESSION_KEY_RETREVAL        0x00001020
#define HDCP_ERR_SESSION_KEY_INJECTION       0x00001021
#define HDCP_ERR_CANNOT_SET_STORGE_DATA      0x00001022
#define HDCP_ERR_NOT_INITIALIZED             0x00001023
#define HDCP_ERR_HEAP_INIT_ERROR             0x00001024
#define HDCP_ERR_SEQ_NUM_V_ROLL_OVER         0x00001025
#define HDCP_ERR_PROTOCOL_ERROR              0x00001026
#define HDCP_ERR_REP_ASK_FOR_REQUTH          0x00001027
#define HDCP_ERR_SEQ_NUM_M_ROLL_OVER         0x00001028
#define HDCP_ERR_SHA_OPS_FAILS               0x00001029
#define HDCP_ERR_M_COMPARE_FAILED            0x0000102A
#define HDCP_ERR_ENCRYPTION_ENABLE_A0        0x0000102B
#define HDCP_ERR_ENCRYPTION_ENABLE_A1        0x0000102C
#define HDCP_NO_ERR_DONT_STORE_KM_DATA       0x0000102D
#define HDCP_ERR_ASD_DRIVER_CALL_FAILED      0x20000000 // this error code is designed to be this value on purpose

/**
 * Structure sizes
 */
#define HDCP_RANDOM_NUMBER_SIZE              16
#define HDCP_OMAC_OUTPUT_SIZE                16
#define HDCP_GUID_SIZE                       16
#define HDCP_SESSION_KEY_SIZE                16
#define HDCP_ENCRYPTED_DATA_SIZE             256
#define HDCP2_MAX_NUM_OF_STREAMS             4   // Maximum streams supported. At the moment only two streams ( One Audio + One Video)
#define HDCP2_DATA_INPUT_SIZE_16_IN_BYTES    16
#define HDCP14_MAX_NUMBER_DOWNSTREAM_DEVICES 127
#define HDCP14_SIZE_OF_KSV                   5

// Command IDs.
// Host message IDs
#define TL_HDCP_CMD_ID_USE_HDCP2_0_1        0x00000001  // Set the test vector Id, only for testing purpose
#define TL_HDCP_CMD_ID_USE_HDCP2_0_2        0x00000002  // Set the test vector Id, only for testing purpose
#define TL_HDCP_CMD_ID_USE_HDCP2_2_1        0x00000003  // Set the test vector Id, only for testing purpose
#define TL_HDCP_CMD_ID_USE_HDCP2_2_2        0x00000004  // Set the test vector Id, only for testing purpose


// Structures of input and output buffers.
#define HDCP2_API_VERSION               0x00000001
#define HDCP2_MAX_DEVICE_COUNT          31              // The maximum number of receiver devices
#define HDCP2_MAX_DEVICE_DEPTH          4               // The maximum depth of receiver devices
#define HDCP2_MAX_TX_COUNT              2               // Support 2 transmitters
#define HDCP2_MAX_SIZE_OF_TRANSMIT_BUF  129             // size of the transmitter buffer
#define HDCP2_MAX_SIZE_OF_RECV_BUF      (524+12)        // size of max peer receiver buffer + receiver info
#define HDCP2_SET_ENABLE                1               // Direction from the host to enable encryption
#define HDCP2_UNSET_ENABLE              0               // Direction from the host to disable the encryption

// Transmitter and receiver message IDs
#define HDCP2_MSG_NULL                              0x00000001  // Trans/recv NULL message, start authentication
#define HDCP2_MSG_AKE_INIT                          0x00000002  // Transmitter AKE_Init message, start authentication
#define HDCP2_MSG_AKE_SEND_CERT                     0x00000003  // Receiver AKE_Send_Cert message, send certificate
#define HDCP2_MSG_AKE_NO_STORED_KM                  0x00000004  // Transmitter AKE_No_Stored_km message, use new created master key km
#define HDCP2_MSG_AKE_STORED_KM                     0x00000005  // Transmitter AKE_Stored_km message, use stored master key km
#define HDCP2_MSG_AKE_SEND_RRX                      0x00000006  // Receiver AKE_Send_rrx message, get variable rrx
#define HDCP2_MSG_AKE_SEND_H_PRIME                  0x00000007  // Receiver AKE_Send_H_prime message, get veriable H'
#define HDCP2_MSG_AKE_SEND_PAIRING_INFO             0x00000008  // Receiver AKE_Send_Pairing_Info message, get pairing info
#define HDCP2_MSG_LC_INIT                           0x00000009  // Transmitter LC_Init message, start locality
#define HDCP2_MSG_LC_SEND_L_PRIME                   0x0000000A  // Receiver LC_Send_L_prime message, get variable L'
#define HDCP2_MSG_SKE_SEND_EKS                      0x0000000B  // Transmitter SKE_Send_Eks message, send encrypted session key Eks
#define HDCP2_MSG_REPEATER_AUTH_SND_RECEIVERID_LIST 0x0000000C  // Receiver RepeaterAuth_Send_ReceiverID_List message, get receiver ID list
#define HDCP2_MSG_RTT_READY                         0x0000000D  // Receiver RTT_Ready message, locality Round Trip Time check ready
#define HDCP2_MSG_RTT_CHALLAENGE                    0x0000000E  // Transmitter RTT_Challenge message, send variable L
#define HDCP2_MSG_REPEATER_AUTH_SEND_ACK            0x0000000F  // Transmitter RepeaterAuth_Send_Ack message, send variable V
#define HDCP2_MSG_REPEATER_AUTH_STREAM_MANAGE       0x00000010  // Transmitter RepeaterAuth_Stream_Manage message
#define HDCP2_MSG_REPATER_AUTH_STREAM_READY         0x00000011  // Receiver RepeaterAuth_Stream_Ready message, get variable M'
#define HDCP2_MSG_RECEIVER_AUTH_STATUS              0x00000012  // Receiver Receiver_AuthStatus message, get flag REAUTH_REQ
#define HDCP2_MSG_AKE_TRANSMITTER_INFO              0x00000013  // Transmitter Transmitter_Infomessage, send transmitter capability mask
#define HDCP2_MSG_AKE_RECEIVER_INFO                 0x00000014  // Receiver AKE_Receiver_Info message, get receiver capability mask
#define HDCP2_MSG_AMD_STREAM_INFO                   0x000000FE  // Proprietary Message id, get stream (stream ctr + information from data path)

// Authentication status
//
#define HDCP2_STATUS_AUTHENTICATION_PENDING           0x00000301  // Not authenticated
#define HDCP2_STATUS_AUTHENTICATED                    0x00000302  // Authenticated
#define HDCP2_STATUS_FAILED                           0x00000303  // failed status
#define HDCP2_STATUS_FAILED_NO_RETRY                  0x000003FE  // Authenticate not allowed
#define HDCP2_STATUS_REQUEST_PASSED                   0x000003FD  // Either enable or disable func. done


/**
 * Session Type/HDCP Version
 */
 typedef enum _HDCP_SESSION_TYPE {
     HDCP_MIRACAST        = 1,          // HDCP Version 2.0 with Miracast
     HDCP22_EXTERNAL_CHIP = 2,    // HDCP Version 2.2 using the external chip
     HDCP_14               = 3                 // HDCP Version 1.4
 } HDCP_SESSION_TYPE;

/**
 * Command Codes
 */

typedef enum _TL_HDCP_COMMAND_CODE {
    TL_HDCP_CMD_ID_INIT                     = 0x00000101,  // Initialize HDCP module
    TL_HDCP_CMD_ID_OPEN_SESSION             = 0x00000102,  // Open an HDCP2 session
    TL_HDCP_CMD_ID_PROCESS_MSG              = 0x00000103,  // Process input receiver messages
    TL_HDCP_CMD_ID_CLOSE_SESSION            = 0x00000104,  // Close an HDCP2 session
    TL_HDCP_CMD_ID_ENABLE_DISABLE_ENC       = 0x00000105,  // Protection lib message to enable or disable the encryption
    TL_HDCP_CMD_ID_TERMINATE                = 0x00000106,  // Terminate the hdcp work
    TL_HDCP_CMD_ID_AUTH_EXTERNAL_CHIP       = 0x00000107,  // Authenticate the HDCP 2.2 External Chip
    TL_HDCP_CMD_ID_UPDATE_STATUS            = 0x00000108,   // Update status for HDCP 2.2 External Chip
    TL_HDCP_CMD_ID_SET_PROTECTION_LEVEL     = 0x00000109,
    TL_HDCP_CMD_ID_GET_HDCP_HW_STATE        = 0x00000110,
    TL_HDCP_CMD_ID_GET_NONCE                = 0x00000111,
    TL_HDCP_CMD_ID_CLOSE_ALL_SESSIONS       = 0x00000112,
    TL_HDCP_CMD_ID_VALIDATE_EC_QUERY        = 0x00000113,
    TL_HDCP_CMD_ID_HDCP_14_GET_AN_AKSV      = 0x00000114,
    TL_HDCP_CMD_ID_HDCP_14_FIRST_PART_AUTH  = 0x00000115,
    TL_HDCP_CMD_ID_HDCP_14_SECOND_PART_AUTH = 0x00000116,
    TL_HDCP_CMD_ID_START_ENCRYPTION         = 0x00000117,
    TL_HDCP_CMD_ID_GET_PROTECTION_LEVEL     = 0x00000118

} TL_HDCP_COMMAND_CODE;

/**
 * Return Error Codes
 */

 typedef enum _HDCP_RETURN_ERROR_CODE {
     HDCP_ERR_INVALID_BUFFER         = 0x00000100,
     HDCP_ERR_BUFFER_TOO_SMALL       = 0x00000101,
     HDCP_ERR_UNSUPPORTED_CMD        = 0x00000102,
     HDCP_ERR_INVALID_SESSION_ID     = 0x00000103,
     HDCP_ERR_UNSUPPORTED_GUID       = 0x00000104,
     HDCP_ERR_INVALID_SESSION_STATE  = 0x00000106
 } HDCP_RETURN_ERROR_CODE;

 /**
  * Return Command Codes
  */

 typedef enum _HDCP_RETURN_COMMAND_CODE {
     HDCP_ERR_INIT                   = 0x00000201,
     HDCP_ERR_OPEN_SESSION           = 0x00000210,
     HDCP_ERR_PROCESS_MSG            = 0x00000220,
     HDCP_ERR_CLOSE_SESSION          = 0x00000230,
     HDCP_ERR_TERMINATE              = 0x00000240
 } HDCP_RETURN_COMMAND_CODE;

/**
 * Termination codes.
 */
#define EXIT_ERROR    ((uint32_t)(-1))


/*
 * HDCP Miracast
 */

typedef struct _HDCP2_SYSTEMTIME
{
    uint32_t LowDateTime;
    uint32_t HighDateTime;
} HDCP2_SYSTEMTIME;

typedef struct _HOST_TO_HDCP2_CMD
{
    uint32_t   CmdId;
    union
    {
        struct _HDCP2_Session_Init_Msg
        {
            HDCP2_SYSTEMTIME    Timeval;
        } HDCP2SessionInitMsg;

        struct _HDCP2_Open_Session
        {
            uint32_t            TransmitterID;
        } HDCP2OpenSession;

        struct _HDCP2_Process_Msg
        {
            uint32_t            SessionID;                              // Session ID
            uint8_t             RxMsgBuf[HDCP2_MAX_SIZE_OF_RECV_BUF];   // Message from the receiver
             uint32_t            RxMsgBufSize;                           // Number of valid uint8_ts in RxMsgBuf
        }HDCP2ProcessMsg;

        struct _HDCP2_Close_Session
        {
            uint32_t            SessionID;                              // HDCP2 transmitter or Dig ID
        } HDCP2CloseSession;

        struct _HDCP2_Set_Protection_Level
        {
             uint32_t            ulDisplayIndex;
             uint32_t            ulProtectionLevel;
         } HDCP2_SetProtectionLevel;
    };
}HOST_TO_HDCP2_CMD;

typedef struct _HDCP2_TO_HOST_CMD
{
    uint32_t                CmdId;                                      // Size of the structure (header + other members)
    uint32_t                SessionID;                                  // HDCP2 Session ID
    uint32_t                HDCP2AuthStat;                              // HDCP2 authentication status
    uint32_t                MsgProcessStat;                             // Internal process status:
    uint32_t                TimeDuration;                               // The =0 mean no time start, 0> start the timer
    uint32_t                TxMsgSize;                                  // Size of the tx message
    uint8_t                 TxMsgBuf[HDCP2_MAX_SIZE_OF_TRANSMIT_BUF];   // Size = HDCP2_TRANSMITTER_MSG
}HDCP2_TO_HOST_CMD;

typedef struct _HDCP2_IN_MSG
{
    HOST_TO_HDCP2_CMD       HostToHDCP;
}HDCP2_IN_MSG;

typedef struct _HDCP2_OUT_MSG
{
    HDCP2_TO_HOST_CMD       HDCPToHost;
}HDCP2_OUT_MSG;

typedef struct _MIRACAST_MESSAGE
{
    tciCommandHeader_t        CommandHeader;
    tciResponseHeader_t       ResponseHeader;
    HDCP2_IN_MSG              CmdHDCPCmdInput;
    HDCP2_OUT_MSG             RspHDCPCmdOutput;
 } MIRACAST_MESSAGE;

typedef struct _HDCP2_RECEIVER_CERT
{
    uint8_t aucReceiverID[5];                                 // 40-bit receiver ID
    uint8_t aucReceiverPublicKey_n[128];                      // 1048-bit receiver public key
    uint8_t aucReceiverPublicKey_e[3];
    uint8_t aucReserved[2];                                   // 4-bit protocol descriptor + 12-bit reserved
    uint8_t aucDCPSignature[384];                             // 3027-bit DCP LLC Signature
} HDCP2_RECEIVER_CERT;

typedef struct _HDCP2_RECEIVER_MSG
{
    uint8_t ucMsg_id;                                         // Message ID

    union
    {
        struct AKE_Send_Cert
        {
            uint8_t             ucREPEATER;                // Flag indicates the down stream is a repeater
            HDCP2_RECEIVER_CERT stCertrx;                  // Receiver certificate
        } AKESendCert;

        struct AKE_Send_rrx
        {
            uint8_t                 aucRrx[8];                // Variable rrx
        } AKESEndRrx;

        struct AKE_Send_H_prime
        {
            uint8_t                 ucHprime[32];             // Variable H'
        } AKESendHPrime;

        struct AKE_Send_Pairing_Info
        {
            uint8_t aucEkh_km[16];                            // Encrypted master key with kh
        } AKESendPairingInfo;

        struct LC_Send_L_prime
        {
            uint8_t aucLprime[32];                            // Variable L'
        } LCSendLPrime;

        struct LC_Send_L_Prime_Ver2_1
        {
            uint8_t aucLprime[16];                            // Variable  MSB L' 128 bit size
        } LCSendLPrimeVer2_1;

        struct _RepeaterAuth_Send_ReceiverID_List             // This message is HDCP2.0 standard specfic
        {
            uint8_t ucMAX_DEVS_EXCEEDED;                      // Flag indicates the maximum number of receivers exceeded
            uint8_t ucMAX_CASCADE_EXCEEDED;                   // Flag indicates the maximum receiver depth exceeded
            uint8_t ucDEVICE_COUNT;                           // Receiver count
            uint8_t ucDEPTH;                                  // Receiver depth
            uint8_t aucVprime[32];                            // Variable V'
            uint8_t aucReceiver_ID[HDCP2_MAX_DEVICE_COUNT][5];// Receiver ID list
        } RepeaterAuthSendReceiverIDList;

        struct _RepeaterAuth_Send_ReceiverID_List_Ver2_1           // This message is HDCP2.1 standard specfic
        {
            uint8_t ucMAX_DEVS_EXCEEDED;                           // Flag indicates the maximum number of receivers exceeded
            uint8_t ucMAX_CASCADE_EXCEEDED;                        // Flag indicates the maximum receiver depth exceeded
            uint8_t ucDEVICE_COUNT;                                // Receiver count
            uint8_t ucDEPTH;                                       // Receiver depth
            uint8_t ucHDCP2_0_REPEATER_DOWNSTREAM;                 // Set to 1(true) if downstream path have hdcp2.0 compliant receiver
            uint8_t ucHDCP1_DEVICE_DOWNSTREAM;                     // Set to 1(True) if downstream path have hdcp1.x compliant receiver
            uint8_t aucSeqNumV[3];                                 // Unique Seq Number received from repeater
            uint8_t aucVprime[HDCP2_DATA_INPUT_SIZE_16_IN_BYTES];  // Variable V'  128 bit LSB value
            uint8_t aucReceiver_ID[HDCP2_MAX_DEVICE_COUNT][5];     // Receiver ID list
        } RepeaterAuthSendReceiverIDListVer2_1;

        struct _RepeaterAuth_Stream_Ready
        {
            uint8_t aucMprime[32];                             // Variable M'
        } RepeaterAuthStreamReady;

        struct _Receiver_AuthStatus
        {
            uint8_t   aucLength[2];                            // 16 bit Length of the combined(msg_id + usLength + ucReauthReq) = 4
            uint8_t   ucReauthReq;                             // RE AUTHENTICATION REQUEST , 1 = to reinitiate the authencation procedure
        } ReceiverAuthStatus;

        struct _AKE_Receiver_Info
        {
            uint8_t   aucLength[2];                            // Length of the combined (msg_id + usLen + version+ capability mask) =6
            uint8_t   ucVersion;                               // Version
            uint8_t   aucReceiverCapabilityMask[2];            // 16 bit capability mask
        } AKEReceiverInfo;

        struct AMD_Stream_Info
        {
            uint8_t ucNumOfValidStream;                          // Valid Stream Count
            uint8_t aucStreamCtr[HDCP2_MAX_NUM_OF_STREAMS][4];   // array of 32 bit counter value for stream counter
            uint8_t aucPID[HDCP2_MAX_NUM_OF_STREAMS][2];         // 16 bit array of corresponding PID value of the stream
        } AMDStreamInfo;
    };
} HDCP2_RECEIVER_MSG;

typedef struct _STREAM_INFO
{                                           // This is the protocol standard structure meant for storing to be encrypteds treams informations
     uint8_t     aucStreamCtr[4];            // The UINT is 4 uint8_t array Stream Counter
    uint8_t     aucContentStreamID[2];      // 16 bit PID value of the Stream
    uint8_t     ucType;                     // Type of stream control. This value is fixed 0 at present
} STREAM_INFO;

// HDCP2 transmitter messages
typedef struct _HDCP2_TRANSMITTER_MSG
{
    uint8_t ucMsg_id;                                        // Message ID

    union
    {
        struct AKE_Init
        {
            uint8_t aucRtx[8];                               // 64-bit variable rtx
        } AKEInit;

        struct AKE_No_Stored_km
        {
            uint8_t aucEkpub_km[128];                        // 1024-bit encrypted master key with kpubrx
        } AKENoStoredKm;

        struct AKE_Stored_km
        {
            uint8_t aucEkh_km[16];                           // 128-bit encrypted master key km
            uint8_t m[16];                                   // 128-bit variable m
        } AKEStoredKm;

        struct LC_Init
        {
            uint8_t rn[8];                                   // 64-bit variable rn
        } LCInit;

        struct SKE_Send_Eks
        {
            uint8_t aucEdkey_ks[16];                         // 128-bit encrypted session key ks
            uint8_t riv[8];                                  // 64-bit veriable riv
        } SKESendEks;

        struct _RTT_Challange
        {
            uint8_t aucLprime[HDCP2_DATA_INPUT_SIZE_16_IN_BYTES];                           // LSB 128 bit value for L'
        } RTTChallange;

        struct _RepeaterAuth_Send_Ack
        {
            uint8_t aucVprime[HDCP2_DATA_INPUT_SIZE_16_IN_BYTES];                            // MSB 128 bit V value
        } RepeaterAuthSendAck;

        struct _RepeaterAuth_Stream_Manage
        {
            uint8_t        aucSeqNumM[3];                         // Seq Num value
            uint8_t        aucK[2];                               // Num Streams
            STREAM_INFO astStreams[HDCP2_MAX_NUM_OF_STREAMS];
        } RepeaterAuthStreamManage;

        struct _AKE_Tx_Info
        {
             uint8_t   ausLen[2];                              // Length of Msg = msg_id + length + version + cap mask = 6 uint8_ts
            uint8_t   ucVersion;                              // Version of HDCP
            uint8_t   ausTxCapMask[2];                        // Transmitter capability mask
        } AKETxInfo;
    };
} HDCP2_TRANSMITTER_MSG;

/*
 * HDCP_EC
 */

 typedef enum _HDPCEC_SESSION_STATUS
 {
     STATUS_SESSION_OPEN = 0,
     STATUS_SESSION_AUTHENTICATED = 1
 } HDPCEC_SESSION_STATUS;

#define MAX_HDCP_EC_QUERY_SIZE (31 * 5 + 2)

 typedef struct _HDCP_EC_IN_MSG
 {
     uint32_t ulDisplayIndex;
     union
     {
         struct {
             uint32_t ulDisplayIndex;
         } OpenSession;

         struct {
             uint32_t ulDisplayIndex;
             uint8_t  bRCON[16];
             uint8_t  bCertificate[512];
             uint32_t ulCertificateSize;
         } AuthenticateExternalChip;

         struct {
             uint32_t bStatus;
         } UpdateStatus;

         struct
         {
             uint32_t       ulEncryptionState;
             uint32_t       ulContentType;
             uint32_t       ulRxHDCPVersion;
             uint8_t        HMACValue[32];
         } SendHardwareState;

         struct
         {
             uint32_t       ulQuerySize;
             uint8_t        bQueryBytes[MAX_HDCP_EC_QUERY_SIZE];
             uint8_t        HMACValue[32];
         } ValidateQuery;
     };
 } HDCP_EC_IN_MSG;


 typedef struct _HDCP_EC_OUT_MSG
 {
     uint32_t bSuccess;
     union
     {
         struct {
             uint8_t EncryptedKMAC[128];
         } AuthenticateExternalChip;

         struct {
             uint8_t bNonce[32];
         } GetNonce;
     };
 } HDCP_EC_OUT_MSG;

 typedef enum _HDCP_EC_RESPONSE_CODE
 {
     HDCP_EC_STATUS_SUCCESS                  = 0x01,
     HDCP_EC_STATUS_FAILURE                  = 0x02,
     HDCP_EC_STATUS_FAILED_HASH              = 0x03,
     HDCP_EC_STATUS_INVALID_HMAC             = 0x04,
     HDCP_EC_STATUS_FAILED_SETTING_ASDDRIVER = 0x05,
 } HDCP_EC_RESPONSE_CODE;

 typedef struct _HDCP_EC_MESSAGE {
    tciCommandHeader_t          CommandHeader;
    tciResponseHeader_t         ResponseHeader;
    HDCP_EC_IN_MSG              CmdHDCPCmdInput;
    HDCP_EC_OUT_MSG             RspHDCPCmdOutput;
} HDCP_EC_MESSAGE;

typedef enum _HDCP_14_RESPONSE_CODE
{
    HDCP_14_STATUS_SUCCESS                   = 0x01,
    HDCP_14_STATUS_GENERIC_FAILURE           = 0x02,
    HDCP_14_STATUS_FAILED_ALLOCATING_SESSION = 0x03,
    HDCP_14_STATUS_FAILED_SETUP_TX              = 0x04,
    HDCP_14_STATUS_TCI_BUFFER_NOT_SET_CORRECTLY = 0x05,
    HDCP_14_STATUS_VHX_ERROR                    = 0x06,
    HDCP_14_STATUS_SESSION_NOT_CLOSED_PROPERLY  = 0x07,
    HDCP_14_STATUS_SRM_FAILURE                  = 0x08,
} HDCP_14_RESPONSE_CODE;

typedef enum _HDCP_AUTH_FAIL_INFO
{
    HDCP_AUTH_INFO_RESET_VALUE                      = 0,
    HDCP_AUTH_INFO_SOFTWARE_DISABLED_AUTHENTICATION = 1,
    HDCP_AUTH_INFO_AN_WRITTEN                       = 2,
    HDCP_AUTH_INFO_INVALID_AKSV                     = 3,
    HDCP_AUTH_INFO_INVALID_BKSV                     = 4,
    HDCP_AUTH_INFO_RI_MISMATCH                      = 5,
    HDCP_AUTH_INFO_THREE_CONSECUTIVE_PJ_MISMATCHES  = 6,
    HDCP_AUTH_INFO_HPD_DISCONNECT                   = 7
} HDCP_AUTH_FAIL_INFO;


typedef enum _HDCP_14_CONNECTOR_TYPE
{
    HDCP_14_CONNECTOR_TYPE_VGA = 0,
    HDCP_14_CONNECTOR_TYPE_SVideo = 1,
    HDCP_14_CONNECTOR_TYPE_CompositeVideo = 2,
    HDCP_14_CONNECTOR_TYPE_ComponentVideo = 3,
    HDCP_14_CONNECTOR_TYPE_DVI = 4,
    HDCP_14_CONNECTOR_TYPE_HDMI = 5,
    HDCP_14_CONNECTOR_TYPE_DP = 6,
    HDCP_14_CONNECTOR_TYPE_ForceDWORD = 0x7fffffff  /* force 32-bit size enum */
} HDCP_14_CONNECTOR_TYPE;

 typedef struct _HDCP_14_IN_MSG
 {
     uint8_t DigId;
     union
     {
         struct {
             uint32_t bIsDualLink;
             uint32_t DDCLine;
             uint8_t Bcaps;
             HDCP_14_CONNECTOR_TYPE ConnectorType;
         } OpenSession;

         struct {
             uint8_t BksvPrimary[5];
             uint8_t BksvSecondary[5];
             uint8_t Bcaps;
             uint8_t RNotPrime[2];
         } FirstPartAuth;

         struct {
             union
             {
                 uint8_t  HdmiBStatus[2];
                 uint8_t  DpBStatus[1];
             };
             uint8_t  BInfo[2];
             uint8_t  KSVList[HDCP14_MAX_NUMBER_DOWNSTREAM_DEVICES * HDCP14_SIZE_OF_KSV];
             uint32_t KSVListSize;
             uint8_t  Pj;
             uint8_t  VPrime[20];
         } SecondPartAuth;

	struct {
             uint32_t bIsDualLink;
         } GetProtectionLevel;
     };
 } HDCP_14_IN_MSG;


 typedef struct _HDCP_14_OUT_MSG
 {
     uint8_t bResponseCode; /* HDCP_14_RESPONSE_CODE */
     union
     {
         struct {
             uint8_t AInfo;
             uint8_t AnPrimary[8];
             uint8_t AksvPrimary[5];
             // For DualLink
             uint8_t AnSecondary[8];
             uint8_t AksvSecondary[5];
         } OpenSession;

	struct {
             uint32_t ProtectionLevel;
             uint32_t HdcpAuthFailInfo;
         } GetProtectionLevel;
     };

 } HDCP_14_OUT_MSG;

 typedef struct _HDCP_14_MESSAGE {
     tciCommandHeader_t          CommandHeader;
     tciResponseHeader_t         ResponseHeader;
     HDCP_14_IN_MSG              CmdHDCPCmdInput;
     HDCP_14_OUT_MSG             RspHDCPCmdOutput;
 } HDCP_14_MESSAGE;

/**
 * Interop Structs
 */

 typedef struct _HDCP_TCI
 {
     uint32_t               bNewSemantics;
     uint32_t               eHDCPSessionType;
     uint16_t               eHDCPCommand; /* enum TL_HDCP_COMMAND_CODE */
     union
     {
         MIRACAST_MESSAGE      Message; // For backwards compatability, this gets the mononymous variable Message
         HDCP_EC_MESSAGE       HDCP_EC_Message;
         HDCP_14_MESSAGE       HDCP_14_Message;
     };
 } HDCP_TCI;

 #endif /* __tl_hdcp_public__dot__h__ */
