/* file-desc **************************************************************************************
 * @filename    CircleQueue.c
 * @brief       Generic fixed-capacity circular queue (ring buffer) implementation.
 * @note        Provides O(1) push/pop with optional circular overwrite when full.
 **************************************************************************************************/

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__common__.h"
#include "./CircleQueue.h"
#include <string.h>

/* macros *****************************************************************************************
 **************************************************************************************************/

/* definitions ************************************************************************************
 **************************************************************************************************/

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* public variables *******************************************************************************
 **************************************************************************************************/

/* private variables ******************************************************************************
 **************************************************************************************************/

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* public functions *******************************************************************************
 **************************************************************************************************/

Err_t CircleQueueInit(CircleQueue_t *p_queue,
                      void          *p_buffer,
                      uint32_t       elem_size,
                      uint32_t       capacity,
                      uint8_t        allow_overwrite);
Err_t CircleQueueReset(CircleQueue_t *p_queue);
Err_t CircleQueuePush(CircleQueue_t *p_queue, const void *p_elem);
Err_t CircleQueuePop(CircleQueue_t *p_queue, void *p_elem_out);
Err_t CircleQueueIsEmpty(const CircleQueue_t *p_queue, uint8_t *p_is_empty);
Err_t CircleQueueIsFull(const CircleQueue_t *p_queue, uint8_t *p_is_full);

/* private functions ******************************************************************************
 **************************************************************************************************/

/* function implementations ***********************************************************************
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
                      uint8_t        allow_overwrite) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate queue descriptor and storage buffer pointers */
    if ((NULL == p_queue) || (NULL == p_buffer)) {
        APP_ERR("Null pointer in CircleQueueInit!");
        return APP_ERROR_NULL;
    }

    /* Validate non-zero element size and capacity */
    if ((0U == elem_size) || (0U == capacity)) {
        APP_ERR("Invalid elem_size or capacity in CircleQueueInit!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_queue->p_Buffer       = p_buffer;
    p_queue->ElemSize       = elem_size;
    p_queue->Capacity       = capacity;
    p_queue->Head           = 0U;
    p_queue->Tail           = 0U;
    p_queue->Count          = 0U;
    p_queue->AllowOverwrite = (0U != allow_overwrite) ? 1U : 0U;

    /* Return success after initializing circular queue */
    return APP_SUCCESS;
}

/* Reset a circular queue to the empty state without deallocating its buffer
 * @param p_queue[in/out] Pointer to the circular queue structure
 * @return APP_SUCCESS on success, or APP_ERROR_NULL if p_queue is NULL
 * @note Sets Head, Tail, and Count to 0U
 */
Err_t CircleQueueReset(CircleQueue_t *p_queue) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate queue pointer */
    if (NULL == p_queue) {
        APP_ERR("p_queue is NULL in CircleQueueReset!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_queue->Head  = 0U;
    p_queue->Tail  = 0U;
    p_queue->Count = 0U;

    /* Return success after resetting circular queue indices */
    return APP_SUCCESS;
}

/* Push a single element into the circular queue
 * @param p_queue[in/out] Pointer to the circular queue structure
 * @param p_elem[in] Pointer to the element data to copy into the queue
 * @return APP_SUCCESS on success, or APP_ERROR_QUEUE_FULL if full and AllowOverwrite is 0U
 * @note When AllowOverwrite is 1U and the queue is full, the oldest item at Head is overwritten
 */
Err_t CircleQueuePush(CircleQueue_t *p_queue, const void *p_elem) {
    uint8_t *p_dst;

    #if (1U == PARAM_CHECK_EN)
    /* Validate queue descriptor, element pointer, and underlying buffer */
    if ((NULL == p_queue) || (NULL == p_elem) || (NULL == p_queue->p_Buffer)) {
        APP_ERR("Null pointer in CircleQueuePush!");
        return APP_ERROR_NULL;
    }

    /* Validate initialized capacity and element size */
    if ((0U == p_queue->Capacity) || (0U == p_queue->ElemSize)) {
        APP_ERR("Uninitialized CircleQueue in CircleQueuePush!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Check if the circular queue has reached maximum capacity */
    if (p_queue->Count >= p_queue->Capacity) {
        /* Reject push if overwrite mode is disabled */
        if (0U == p_queue->AllowOverwrite) {
            APP_ERR("CircleQueue is full!");
            return APP_ERROR_QUEUE_FULL;
        }

        /* Overwrite mode enabled: advance Head to drop the oldest element */
        p_queue->Head  = (p_queue->Head + 1U) % p_queue->Capacity;
        p_queue->Count = p_queue->Capacity - 1U;
    }

    /* Calculate byte address of the Tail slot and copy the new element */
    p_dst = (uint8_t *)p_queue->p_Buffer + (p_queue->Tail * p_queue->ElemSize);
    (void)memcpy(p_dst, p_elem, (size_t)p_queue->ElemSize);

    /* Advance Tail index circularly and increment element count */
    p_queue->Tail = (p_queue->Tail + 1U) % p_queue->Capacity;
    p_queue->Count++;

    /* Return success after pushing element into circular queue */
    return APP_SUCCESS;
}

/* Pop the oldest element from the circular queue
 * @param p_queue[in/out] Pointer to the circular queue structure
 * @param p_elem_out[out] Destination buffer where the popped element will be copied
 * @return APP_SUCCESS on success, or APP_ERROR_QUEUE_EMPTY if the queue is empty
 * @note Advances Head and decrements Count
 */
Err_t CircleQueuePop(CircleQueue_t *p_queue, void *p_elem_out) {
    const uint8_t *p_src;

    #if (1U == PARAM_CHECK_EN)
    /* Validate queue descriptor, output pointer, and underlying buffer */
    if ((NULL == p_queue) || (NULL == p_elem_out) || (NULL == p_queue->p_Buffer)) {
        APP_ERR("Null pointer in CircleQueuePop!");
        return APP_ERROR_NULL;
    }

    /* Validate initialized capacity and element size */
    if ((0U == p_queue->Capacity) || (0U == p_queue->ElemSize)) {
        APP_ERR("Uninitialized CircleQueue in CircleQueuePop!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Check if the circular queue is empty */
    if (0U == p_queue->Count) {
        /* Return queue empty status without logging an error (normal polling condition) */
        return APP_ERROR_QUEUE_EMPTY;
    }

    /* Calculate byte address of the Head slot and copy the oldest element out */
    p_src = (const uint8_t *)p_queue->p_Buffer + (p_queue->Head * p_queue->ElemSize);
    (void)memcpy(p_elem_out, p_src, (size_t)p_queue->ElemSize);

    /* Advance Head index circularly and decrement element count */
    p_queue->Head = (p_queue->Head + 1U) % p_queue->Capacity;
    p_queue->Count--;

    /* Return success after popping element from circular queue */
    return APP_SUCCESS;
}

/* Check whether the circular queue is currently empty
 * @param p_queue[in] Pointer to the circular queue structure
 * @param p_is_empty[out] Outputs 1U if empty, 0U otherwise
 * @return APP_SUCCESS on success, or APP_ERROR_NULL if parameters are NULL
 * @note Does not modify queue state
 */
Err_t CircleQueueIsEmpty(const CircleQueue_t *p_queue, uint8_t *p_is_empty) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate pointers */
    if ((NULL == p_queue) || (NULL == p_is_empty)) {
        APP_ERR("Null pointer in CircleQueueIsEmpty!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    *p_is_empty = (0U == p_queue->Count) ? 1U : 0U;

    /* Return success after checking empty status */
    return APP_SUCCESS;
}

/* Check whether the circular queue is currently full
 * @param p_queue[in] Pointer to the circular queue structure
 * @param p_is_full[out] Outputs 1U if full, 0U otherwise
 * @return APP_SUCCESS on success, or APP_ERROR_NULL if parameters are NULL
 * @note Does not modify queue state
 */
Err_t CircleQueueIsFull(const CircleQueue_t *p_queue, uint8_t *p_is_full) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate pointers */
    if ((NULL == p_queue) || (NULL == p_is_full)) {
        APP_ERR("Null pointer in CircleQueueIsFull!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    *p_is_full = (p_queue->Count >= p_queue->Capacity) ? 1U : 0U;

    /* Return success after checking full status */
    return APP_SUCCESS;
}

/* reserved ***************************************************************************************
 **************************************************************************************************/

