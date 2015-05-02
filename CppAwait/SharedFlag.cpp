/*
* Copyright 2012-2015 Valentin Milea
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

#include "ConfigPrivate.h"
#include <CppAwait/impl/SharedFlag.h>

#ifdef UT_ENABLE_SHARED_FLAG_POOL

#include <boost/pool/pool_alloc.hpp>

namespace ut {

typedef boost::fast_pool_allocator<
    SharedFlag,
    boost::default_user_allocator_new_delete,
    boost::details::pool::default_mutex> SharedFlagAllocator;

SharedFlag allocateSharedFlag(void *value)
{
    return std::allocate_shared<void *>(SharedFlagAllocator(), value);
}

}

#else

namespace ut {

SharedFlag allocateSharedFlag(void *value)
{
    return std::make_shared<void *>(value);
}

}

#endif
