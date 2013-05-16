/*
* Copyright 2012-2013 Valentin Milea
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

#include "../Config.h"
#include <boost/config.hpp>

#ifdef BOOST_MSVC
# pragma warning(push)
# pragma warning(disable : 4512) // assignment operator could not be generated
#endif

#include <boost/signals2.hpp>

#ifdef BOOST_MSVC
# pragma warning(pop)
#endif
