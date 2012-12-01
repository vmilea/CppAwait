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

#include "stdafx.h"
#include <Looper/Chrono.h>
#include <boost/chrono.hpp>

namespace loo {

static boost::chrono::nanoseconds sBaseTime = boost::chrono::nanoseconds(0);

void rebaseMonotonicTime()
{
    sBaseTime = boost::chrono::high_resolution_clock::now().time_since_epoch();
}

Timepoint getMonotonicTime()
{
    boost::chrono::nanoseconds elapsed = boost::chrono::high_resolution_clock::now().time_since_epoch() - sBaseTime;
    return Timepoint((int64_t) elapsed.count());
}

}
