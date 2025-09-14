/*
 * _Debug.h
 *
 *  Created on: Aug 26, 2025
 *      Author: samna
 */

#ifndef INC__DEBUG_H_
#define INC__DEBUG_H_

#define DEBUG_PRINT 1

#if (DEBUG_PRINT == 1)
  #include <stdio.h>
  #define PRINT_DBG(...)              printf(__VA_ARGS__)
#else
  #define PRINT_DBG(...)
#endif

#endif /* INC__DEBUG_H_ */
