/*
 * _Debug.h
 *
 *  Created on: Aug 26, 2025
 *      Author: samna
 *
 * PRINT_DBG() output becomes EVT_LOG frames (see Comms/log.c). bluenrg_conf.h
 * defines the same macro for the BlueNRG middleware; whichever is seen first
 * wins, and both expand to printf().
 */

#ifndef INC__DEBUG_H_
#define INC__DEBUG_H_

#define DEBUG_PRINT 1

#ifndef PRINT_DBG
#if (DEBUG_PRINT == 1)
  #include <stdio.h>
  #define PRINT_DBG(...)              printf(__VA_ARGS__)
#else
  #define PRINT_DBG(...)
#endif
#endif

#endif /* INC__DEBUG_H_ */
