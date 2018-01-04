/*
 * Copyright (c) 2017 Simon Goldschmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Simon Goldschmidt <goldsimon@gmx.de>
 *
 */

/* lwIP includes. */
#include "lwip/debug.h"
#include "lwip/def.h"
#include "lwip/sys.h"
#include "lwip/mem.h"
#include "lwip/stats.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/** Set this to 1 to use a mutex for SYS_ARCH_PROTECT critical regions.
 * Default is 0 and locks interrupts.
 */
#ifndef LWIP_FREERTOS_PROTECT_USES_MUTEX
#define LWIP_FREERTOS_PROTECT_USES_MUTEX  0
#endif

#if SYS_LIGHTWEIGHT_PROT && LWIP_FREERTOS_PROTECT_USES_MUTEX
static SemaphoreHandle_t sys_arch_protect_mutex;
#endif

/* Initialize this module (see description in sys.h) */
void
sys_init(void)
{
#if SYS_LIGHTWEIGHT_PROT && LWIP_FREERTOS_PROTECT_USES_MUTEX
  /* initialize sys_arch_protect global mutex */
  sys_arch_protect_mutex = xSemaphoreCreateRecursiveMutex();
  LWIP_ASSERT("failed to create sys_arch_protect mutex",
    sys_arch_protect_mutex != NULL);
#endif /* SYS_LIGHTWEIGHT_PROT && LWIP_FREERTOS_PROTECT_USES_MUTEX */
}

#if configUSE_16_BIT_TICKS == 1
#error This port requires 32 bit ticks or timer overflow will fail
#endif

u32_t
sys_now(void)
{
  return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

u32_t
sys_jiffies(void)
{
  return sys_now();
}

#if SYS_LIGHTWEIGHT_PROT

sys_prot_t
sys_arch_protect(void)
{
#if LWIP_FREERTOS_PROTECT_USES_MUTEX
  BaseType_t ret;
  LWIP_ASSERT("sys_arch_protect_mutex != NULL", sys_arch_protect_mutex != NULL);

  ret = xSemaphoreTakeRecursive(sys_arch_protect_mutex, portMAX_DELAY);
  LWIP_ASSERT("sys_arch_protect failed to take the mutex", ret == pdTRUE);
#else /* LWIP_FREERTOS_PROTECT_USES_MUTEX */
  taskENTER_CRITICAL();
#endif /* LWIP_FREERTOS_PROTECT_USES_MUTEX */
  return 1;
}

void
sys_arch_unprotect(sys_prot_t pval)
{
#if LWIP_FREERTOS_PROTECT_USES_MUTEX
  BaseType_t ret;
  LWIP_ASSERT("sys_arch_protect_mutex != NULL", sys_arch_protect_mutex != NULL);

  ret = xSemaphoreGiveRecursive(sys_arch_protect_mutex);
  LWIP_ASSERT("sys_arch_unprotect failed to give the mutex", ret == pdTRUE);
#else /* LWIP_FREERTOS_PROTECT_USES_MUTEX */
  taskEXIT_CRITICAL();
#endif /* LWIP_FREERTOS_PROTECT_USES_MUTEX */
  LWIP_UNUSED_ARG(pval);
}

#endif /* SYS_LIGHTWEIGHT_PROT */

void
sys_arch_msleep(u32_t delay_ms)
{
  TickType_t delay_ticks = delay_ms / portTICK_RATE_MS;
  vTaskDelay(delay_ticks);
}

#if !LWIP_COMPAT_MUTEX

/* Create a new mutex*/
err_t
sys_mutex_new(sys_mutex_t *mutex)
{
  LWIP_ASSERT("mutex != NULL", mutex != NULL);

  mutex->mut = xSemaphoreCreateRecursiveMutex();
  if(mutex->mut == NULL) {
    SYS_STATS_INC(mutex.err);
    return ERR_MEM;
  }
  SYS_STATS_INC_USED(mutex);
  return ERR_OK;
}

void
sys_mutex_lock(sys_mutex_t *mutex)
{
  BaseType_t ret;
  LWIP_ASSERT("mutex != NULL", mutex != NULL);
  LWIP_ASSERT("mutex->mut != NULL", mutex->mut != NULL);

  ret = xSemaphoreTakeRecursive(mutex->mut, portMAX_DELAY);
  LWIP_ASSERT("failed to take the mutex", ret == pdTRUE);
}

void
sys_mutex_unlock(sys_mutex_t *mutex)
{
  BaseType_t ret;
  LWIP_ASSERT("mutex != NULL", mutex != NULL);
  LWIP_ASSERT("mutex->mut != NULL", mutex->mut != NULL);

  ret = xSemaphoreGiveRecursive(mutex->mut);
  LWIP_ASSERT("failed to give the mutex", ret == pdTRUE);
}

void
sys_mutex_free(sys_mutex_t *mutex)
{
  LWIP_ASSERT("mutex != NULL", mutex != NULL);
  LWIP_ASSERT("mutex->mut != NULL", mutex->mut != NULL);

  SYS_STATS_DEC(mutex.used);
  vSemaphoreDelete(mutex->mut);
  mutex->mut = NULL;
}

#endif /* !LWIP_COMPAT_MUTEX */

err_t
sys_sem_new(sys_sem_t *sem, u8_t initial_count)
{
  LWIP_ASSERT("sem != NULL", sem != NULL);
  LWIP_ASSERT("initial_count invalid (not 0 or 1)",
    (initial_count == 0) || (initial_count == 1));

  sem->sem = xSemaphoreCreateBinary();
  if(sem->sem == NULL) {
    SYS_STATS_INC(sem.err);
    return ERR_MEM;
  }
  SYS_STATS_INC_USED(sem);

  if(initial_count == 1) {
    BaseType_t ret = xSemaphoreGive(sem->sem);
    LWIP_ASSERT("sys_sem_new: initial give failed", ret == pdTRUE);
  }
  return ERR_OK;
}

void
sys_sem_signal(sys_sem_t *sem)
{
  BaseType_t ret;
  LWIP_ASSERT("sem != NULL", sem != NULL);
  LWIP_ASSERT("sem->sem != NULL", sem->sem != NULL);

  ret = xSemaphoreGive(sem->sem);
  /* queue full is OK, this is a signal only... */
  LWIP_ASSERT("sys_sem_signal: sane return value",
    (ret == pdTRUE) || (ret == errQUEUE_FULL));
}

u32_t
sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout_ms)
{
  BaseType_t ret;
  LWIP_ASSERT("sem != NULL", sem != NULL);
  LWIP_ASSERT("sem->sem != NULL", sem->sem != NULL);

  if(!timeout_ms) {
    /* wait infinite */
    ret = xSemaphoreTake(sem->sem, portMAX_DELAY);
    LWIP_ASSERT("taking semaphore failed", ret == pdTRUE);
  } else {
    TickType_t timeout_ticks = timeout_ms / portTICK_RATE_MS;
    ret = xSemaphoreTake(sem->sem, timeout_ticks);
    if (ret == errQUEUE_EMPTY) {
      /* timed out */
      return SYS_ARCH_TIMEOUT;
    }
    LWIP_ASSERT("taking semaphore failed", ret == pdTRUE);
  }

  /* Old versions of lwIP required us to return the time waited.
     This is not the case any more. Just returning != SYS_ARCH_TIMEOUT
     here is enough. */
  return 1;
}

void
sys_sem_free(sys_sem_t *sem)
{
  LWIP_ASSERT("sem != NULL", sem != NULL);
  LWIP_ASSERT("sem->sem != NULL", sem->sem != NULL);

  SYS_STATS_DEC(sem.used);
  vSemaphoreDelete(sem->sem);
  sem->sem = NULL;
}

err_t
sys_mbox_new(sys_mbox_t *mbox, int size)
{
  LWIP_ASSERT("mbox != NULL", mbox != NULL);
  LWIP_ASSERT("size > 0", size > 0);

  mbox->mbx = xQueueCreate((UBaseType_t)size, sizeof(void *));
  if(mbox->mbx == NULL) {
    SYS_STATS_INC(mbox.err);
    return ERR_MEM;
  }
  SYS_STATS_INC_USED(mbox);
  return ERR_OK;
}

void
sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
  BaseType_t ret;
  LWIP_ASSERT("mbox != NULL", mbox != NULL);
  LWIP_ASSERT("mbox->mbx != NULL", mbox->mbx != NULL);

  ret = xQueueSendToBack(mbox->mbx, &msg, portMAX_DELAY);
  LWIP_ASSERT("mbox post failed", ret == pdTRUE);
}

err_t
sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
  BaseType_t ret;
  LWIP_ASSERT("mbox != NULL", mbox != NULL);
  LWIP_ASSERT("mbox->mbx != NULL", mbox->mbx != NULL);

  ret = xQueueSendToBack(mbox->mbx, &msg, 0);
  if (ret == pdTRUE) {
    return ERR_OK;
  } else {
    LWIP_ASSERT("mbox trypost failed", ret == errQUEUE_FULL);
    SYS_STATS_INC(mbox.err);
    return ERR_MEM;
  }
}

u32_t
sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout_ms)
{
  BaseType_t ret;
  LWIP_ASSERT("mbox != NULL", mbox != NULL);
  LWIP_ASSERT("mbox->mbx != NULL", mbox->mbx != NULL);

  if(!timeout_ms) {
    /* wait infinite */
    ret = xQueueReceive(mbox->mbx, &(*msg), portMAX_DELAY);
    LWIP_ASSERT("mbox fetch failed", ret == pdTRUE);
  } else {
    TickType_t timeout_ticks = timeout_ms / portTICK_RATE_MS;
    ret = xQueueReceive(mbox->mbx, &(*msg), timeout_ticks);
    if (ret == errQUEUE_EMPTY) {
      /* timed out */
      *msg = NULL;
      return SYS_ARCH_TIMEOUT;
    }
    LWIP_ASSERT("mbox fetch failed", ret == pdTRUE);
  }

  /* Old versions of lwIP required us to return the time waited.
     This is not the case any more. Just returning != SYS_ARCH_TIMEOUT
     here is enough. */
  return 1;
}

u32_t
sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
  BaseType_t ret;
  LWIP_ASSERT("mbox != NULL", mbox != NULL);
  LWIP_ASSERT("mbox->mbx != NULL", mbox->mbx != NULL);

  ret = xQueueReceive(mbox->mbx, &(*msg), 0);
  if (ret == errQUEUE_EMPTY) {
    *msg = NULL;
    return SYS_MBOX_EMPTY;
  }
  LWIP_ASSERT("mbox fetch failed", ret == pdTRUE);

  /* Old versions of lwIP required us to return the time waited.
     This is not the case any more. Just returning != SYS_ARCH_TIMEOUT
     here is enough. */
  return 1;
}

void
sys_mbox_free(sys_mbox_t *mbox)
{
  BaseType_t ret;
  LWIP_ASSERT("mbox != NULL", mbox != NULL);
  LWIP_ASSERT("mbox->mbx != NULL", mbox->mbx != NULL);

  vQueueDelete(mbox->mbx);

  SYS_STATS_DEC(mbox.used);
}

sys_thread_t
sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio)
{
  xTaskHandle rtos_task;
  BaseType_t ret;
  sys_thread_t lwip_thread;
  size_t rtos_stacksize = stacksize / sizeof(StackType_t);

  /* lwIP's lwip_thread_fn matches FreeRTOS' TaskFunction_t, so we can pass the
     thread function without adaption here. */
  ret = xTaskCreate(thread, name, (configSTACK_DEPTH_TYPE)rtos_stacksize, arg, prio, &rtos_task);
  LWIP_ASSERT("task creation failed", ret == pdTRUE);

  lwip_thread.thread_handle = rtos_task;
  return lwip_thread;
}

#if LWIP_NETCONN_SEM_PER_THREAD
#if configNUM_THREAD_LOCAL_STORAGE_POINTERS > 0

sys_sem_t *
sys_arch_netconn_sem_get(void)
{
  void* ret;
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  LWIP_ASSERT("task != NULL", task != NULL);

  ret = pvTaskGetThreadLocalStoragePointer(task, 0);
  return ret;
}

void
sys_arch_netconn_sem_alloc(void)
{
  void *ret;
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  LWIP_ASSERT("task != NULL", task != NULL);

  ret = pvTaskGetThreadLocalStoragePointer(task, 0);
  if(ret == NULL) {
    sys_sem_t sem;
    err_t err = sys_sem_new(&sem, 0);
    LWIP_ASSERT("err == ERR_OK", err == ERR_OK);
    LWIP_ASSERT("sem invalid", sys_sem_valid(&sem));
    vTaskSetThreadLocalStoragePointer(task, 0, &sem);
  }
}

void sys_arch_netconn_sem_free(void)
{
  void* ret;
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  LWIP_ASSERT("task != NULL", task != NULL);

  ret = pvTaskGetThreadLocalStoragePointer(task, 0);
  if(ret != NULL) {
    sys_sem_t *sem = ret;
    sys_sem_free(sem);
    vTaskSetThreadLocalStoragePointer(task, 0, NULL);
  }
}

#else /* configNUM_THREAD_LOCAL_STORAGE_POINTERS > 0 */
#error LWIP_NETCONN_SEM_PER_THREAD needs configNUM_THREAD_LOCAL_STORAGE_POINTERS
#endif /* configNUM_THREAD_LOCAL_STORAGE_POINTERS > 0 */

#endif /* LWIP_NETCONN_SEM_PER_THREAD */
