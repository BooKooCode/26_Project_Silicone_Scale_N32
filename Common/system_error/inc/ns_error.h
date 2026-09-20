#ifndef NS_ERROR_H__
#define NS_ERROR_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup NS_ERRORS_BASE Error Codes Base number definitions
 * @{ */
#define NS_ERROR_BASE_NUM     (0x0)       ///< Global error base
#define NS_ERROR_SDM_BASE_NUM  (0x1000)    ///< SDM error base
#define NS_ERROR_SOC_BASE_NUM  (0x2000)    ///< SoC error base
#define NS_ERROR_STK_BASE_NUM  (0x3000)    ///< STK error base
/** @} */

#define NS_SUCCESS                           (NS_ERROR_BASE_NUM+ 0)  ///< Successful command
#define NS_ERROR_SVC_HANDLER_MISSING         (NS_ERROR_BASE_NUM+ 1)  ///< SVC handler is missing
#define NS_ERROR_SOFTDEVICE_NOT_ENABLED      (NS_ERROR_BASE_NUM+ 2)  ///< SoftDevice has not been enabled
#define NS_ERROR_INTERNAL                    (NS_ERROR_BASE_NUM+ 3)  ///< Internal Error
#define NS_ERROR_NO_MEM                      (NS_ERROR_BASE_NUM+ 4)  ///< No Memory for operation
#define NS_ERROR_NOT_FOUND                   (NS_ERROR_BASE_NUM+ 5)  ///< Not found
#define NS_ERROR_NOT_SUPPORTED               (NS_ERROR_BASE_NUM+ 6)  ///< Not supported
#define NS_ERROR_INVALID_PARAM               (NS_ERROR_BASE_NUM+ 7)  ///< Invalid Parameter
#define NS_ERROR_INVALID_STATE               (NS_ERROR_BASE_NUM+ 8)  ///< Invalid state, operation disallowed in this state
#define NS_ERROR_INVALID_LENGTH              (NS_ERROR_BASE_NUM+ 9)  ///< Invalid Length
#define NS_ERROR_INVALID_FLAGS               (NS_ERROR_BASE_NUM+ 10) ///< Invalid Flags
#define NS_ERROR_INVALID_DATA                (NS_ERROR_BASE_NUM+ 11) ///< Invalid Data
#define NS_ERROR_DATA_SIZE                   (NS_ERROR_BASE_NUM+ 12) ///< Invalid Data size
#define NS_ERROR_TIMEOUT                     (NS_ERROR_BASE_NUM+ 13) ///< Operation timed out
#define NS_ERROR_NULL                        (NS_ERROR_BASE_NUM+ 14) ///< Null Pointer
#define NS_ERROR_FORBIDDEN                   (NS_ERROR_BASE_NUM+ 15) ///< Forbidden Operation
#define NS_ERROR_INVALID_ADDR                (NS_ERROR_BASE_NUM+ 16) ///< Bad Memory Address
#define NS_ERROR_BUSY                        (NS_ERROR_BASE_NUM+ 17) ///< Busy
#define NS_ERROR_CONN_COUNT                  (NS_ERROR_BASE_NUM+ 18) ///< Maximum connection count exceeded.
#define NS_ERROR_RESOURCES                   (NS_ERROR_BASE_NUM+ 19) ///< Not enough resources for operation

typedef uint32_t ret_code_t;

#ifdef __cplusplus
}
#endif
#endif
