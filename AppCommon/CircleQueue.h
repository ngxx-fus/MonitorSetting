/* file-desc **************************************************************************************
 * @filename    CircleQueue.h
 * @brief       Generic fixed-capacity circular queue (ring buffer) interface.
 * @note        Supports optional overwrite-oldest behavior when full.
 **************************************************************************************************/

#ifndef __APP_COMMON_CIRCLE_QUEUE_H__
#define __APP_COMMON_CIRCLE_QUEUE_H__

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__config__.h"
#include "./Error.h"
#include "./Log.h"
#include <stdint.h>

/* macros *****************************************************************************************
 **************************************************************************************************/

/* definitions ************************************************************************************
 **************************************************************************************************/

/* Generic circular queue (ring buffer) descriptor */
struct CircleQueue_st {
    void    *p_Buffer;                  /* Pointer to contiguous element storage array */
    uint32_t ElemSize;                  /* Size in bytes of a single element */
    uint32_t Capacity;                  /* Maximum number of elements the queue can hold */
    uint32_t Head;                      /* Index of the oldest element to pop */
    uint32_t Tail;                      /* Index of the next slot to push */
    uint32_t Count;                     /* Current number of elements stored in the queue */
    uint8_t  AllowOverwrite;            /* 1U: overwrite oldest element when full; 0U: reject */
};
typedef struct CircleQueue_st CircleQueue_t;

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* Initialize a circular queue descriptor over a caller-provided storage buffer
 * @param p_queue[out] Pointer to the circular queue structure to initialize
 * @param p_buffer[in] Pointer to pre-allocated element array of size (elem_size * capacity)
 * @param elem_size[in] Size in bytes of each element
 * @param capacity[in] Maximum number of elements in the buffer
 * @param allow_overwrite[in] 1U to overwrite the oldest element when full, 0U to return error
 * @return APP_SUCCESS on success, or error code if parameters are invalid
 * @note Resets Head, Tail, and Count to 0U
 */
Err_t CircleQueueInit(CircleQueue_t *p_queue,
                      void          *p_buffer,
                      uint32_t       elem_size,
                      uint32_t       capacity,
                      uint8_t        allow_overwrite);

/* Reset a circular queue to the empty state without deallocating its buffer
 * @param p_queue[in/out] Pointer to the circular queue structure
 * @return APP_SUCCESS on success, or APP_ERROR_NULL if p_queue is NULL
 * @note Sets Head, Tail, and Count to 0U
 */
Err_t CircleQueueReset(CircleQueue_t *p_queue);

/* Push a single element into the circular queue
 * @param p_queue[in/out] Pointer to the circular queue structure
 * @param p_elem[in] Pointer to the element data to copy into the queue
 * @return APP_SUCCESS on success, or APP_ERROR_QUEUE_FULL if full and AllowOverwrite is 0U
 * @note When AllowOverwrite is 1U and the queue is full, the oldest item at Head is overwritten
 */
Err_t CircleQueuePush(CircleQueue_t *p_queue, const void *p_elem);

/* Pop the oldest element from the circular queue
 * @param p_queue[in/out] Pointer to the circular queue structure
 * @param p_elem_out[out] Destination buffer where the popped element will be copied
 * @return APP_SUCCESS on success, or APP_ERROR_QUEUE_EMPTY if the queue is empty
 * @note Advances Head and decrements Count
 */
Err_t CircleQueuePop(CircleQueue_t *p_queue, void *p_elem_out);

/* Check whether the circular queue is currently empty
 * @param p_queue[in] Pointer to the circular queue structure
 * @param p_is_empty[out] Outputs 1U if empty, 0U otherwise
 * @return APP_SUCCESS on success, or APP_ERROR_NULL if parameters are NULL
 * @note Does not modify queue state
 */
Err_t CircleQueueIsEmpty(const CircleQueue_t *p_queue, uint8_t *p_is_empty);

/* Check whether the circular queue is currently full
 * @param p_queue[in] Pointer to the circular queue structure
 * @param p_is_full[out] Outputs 1U if full, 0U otherwise
 * @return APP_SUCCESS on success, or APP_ERROR_NULL if parameters are NULL
 * @note Does not modify queue state
 */
Err_t CircleQueueIsFull(const CircleQueue_t *p_queue, uint8_t *p_is_full);

/* reserved ***************************************************************************************
 **************************************************************************************************/

#endif /* __APP_COMMON_CIRCLE_QUEUE_H__ */

