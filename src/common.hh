// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   common.hh
 * @date   May 15, 2021
 * @brief  Brief description here
 */

#pragma once

#include <iostream>

#ifdef RELEASE

#define DBG  0 && std::cerr
#define DBGH 0 && std::cerr
#define DBGF 0 && std::cerr
#define DBGW 0 && std::cerr
#define DBGE 0 && std::cerr
#define FUNC_DUMP
#define LP_ASSERT(cond, msg)

#else
#define DBG (std::cerr)

/* Log to stderr with a decorator */
#define DBGH (DBG << "[\x1B[1m" << std::setw(20)	\
	      << std::string(__FUNCTION__) << "()"	\
	      << "\x1B[0m" << "] ")

/* Log current function to stderr with a decorator */
#define DBGF (DBGH << std::string(__FUNCTION__) << "() called." \
	      << std::endl);

/* Log warning to stderr with decorator */
#define DBGW (DBG << "[\x1B[1m" << std::setw(20)		\
	      << std::string(__FUNCTION__) << "()"		\
	      << "\x1B[0m" << "]\x1B[95m WARNING: \x1B[0m")

/* Log error to stderr with decorator */
#define DBGE (DBG << "[\x1B[31m" << std::setw(20)		\
	      << std::string(__FUNCTION__) << "()"		\
	      << "\x1B[0m" << "]\x1B[95m ERROR: \x1B[0m")

#define FUNC_DUMP (DBGH << " called()" << std::endl)
#endif // ifdef RELEASE

#define P(val) (reinterpret_cast<void*>((val)))
