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

#include "../Config.h"
#include <memory>

namespace ut {

//
// helps ignore callbacks that arrive too late
//

class CompletionGuard
{
public:
    class Token
    {
    public:
        Token()
            : mIsBlocked(false) { }
        
        bool isBlocked() const
        {
            return mIsBlocked;
        }

        void block()
        {
            mIsBlocked = true;
        }
    
    private:
        bool mIsBlocked;
    };

    CompletionGuard()
        : mToken(std::make_shared<Token>()) { }

    ~CompletionGuard()
    {
        block();
    }

    std::shared_ptr<const Token> getToken() const
    {
        return mToken;
    }

    void block()
    {
        mToken->block();
    }

private:
    std::shared_ptr<Token> mToken;
};

}