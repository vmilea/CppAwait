/*
* Copyright 2012 Valentin Milea
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*   http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#pragma once

#include <CppAwait/Config.h>

#if (_MSC_VER < 1700)

// MSVC10 doesn't implement chrono & thread, fallback to Boost

#include <boost/chrono.hpp>
#include <boost/thread.hpp>

namespace loo {
    namespace lchrono {
        using namespace boost::chrono;
    }

    namespace lthread {
        using namespace boost;
    }
}

#else

#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace loo {
    namespace lchrono {
        using namespace std::chrono;
    }

    namespace lthread {
        using namespace std;
    }
}

#endif
