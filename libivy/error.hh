// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   error.hh
 * @date   May 14, 2021
 * @brief  Brief description here
 */

#ifndef IVY_HEADER_LIBIVY_ERROR_H__
#define IVY_HEADER_LIBIVY_ERROR_H__

#define IVY_ERROR(msg)				\
  do {						\
    throw std::runtime_error(msg);		\
  } while (0)

#endif // IVY_HEADER_LIBIVY_ERROR_H__
