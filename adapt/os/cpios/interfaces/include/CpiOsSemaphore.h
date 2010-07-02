// Copyright (c) 2009 Mercury Federal Systems.
// 
// This file is part of OpenCPI.
// 
// OpenCPI is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// OpenCPI is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with OpenCPI.  If not, see <http://www.gnu.org/licenses/>.

// -*- c++ -*-

#ifndef CPIOSSEMAPHORE_H__
#define CPIOSSEMAPHORE_H__

/**
 * \file
 *
 * \brief A counting signaling mechanism between threads.
 *
 * Revision History:
 *
 *     04/19/2005 - Frank Pilhofer
 *                  Initial version.
 *
 */

#include <CpiOsDataTypes.h>
#include <string>

namespace CPI {
  namespace OS {

    /**
     * \brief A counting signaling mechanism between threads.
     *
     * A signaling mechanism between threads within the same process. A
     * semaphore is an unsigned integer variable that can be atomically
     * incremented (using post()) and decremented (using wait()). If the
     * value of the integer is zero, a decrement operation will block
     * until another thread increments the semaphore.
     *
     * A single instance is shared between the threads that want to
     * synchronize.
     */

    class Semaphore {
    public:
      /**
       * Constructor: Initializes the semaphore instance.
       *
       * \param[in] initial Sets the initial value of the semaphore.
       * \throw std::string Operating system error creating the semaphore object.
       */

      Semaphore (unsigned int initial = 1)
        throw (std::string);

      /**
       * Destructor.
       */

      ~Semaphore ()
        throw ();
      
      /**
       * Increase the value of the semaphore.
       *
       * If any threads are blocked in wait(), one of them is awakened.
       *
       * \throw std::string Operating system error.
       */

      void post ()
        throw (std::string);

      /**
       * Decrease the value of the semaphore.
       *
       * If the value of the semaphore is positive, it is decremented,
       * and the operation returns without blocking.
       *
       * If the value of the semaphore is zero, blocks until another
       * thread increases its value using the post() operation.
       *
       * \throw std::string Operating system error.
       */

      void wait ()
        throw (std::string);

    private:
      CPI::OS::uint64_t m_osOpaque[15];

    private:
      /**
       * Not implemented.
       */

      Semaphore (const Semaphore &);

      /**
       * Not implemented.
       */

      Semaphore & operator= (const Semaphore &);
    };

  }
}

#endif
