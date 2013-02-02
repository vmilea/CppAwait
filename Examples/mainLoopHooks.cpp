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

#include "Looper/Looper.h"
#include <CppAwait/Awaitable.h>

namespace ut {

Ticket scheduleDelayed(long delay, Runnable runnable)
{
    return (Ticket) loo::mainLooper().schedule(runnable, delay);
}

bool cancelScheduled(Ticket ticket)
{
    return loo::mainLooper().cancel(ticket);
}

}