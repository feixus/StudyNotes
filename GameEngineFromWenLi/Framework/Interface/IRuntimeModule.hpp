#pragma once

#include "Interface.hpp"

namespace My {
    Interface IRuntimeModule {
    public:
        //析构函数声明为virtual，是为了让子类的析构函数也能被调用
        virtual ~IRuntimeModule() {};

        //纯虚函数，子类必须实现(= 0 是纯虚函数的标志)
        virtual int Initialize() = 0;
        virtual void Finalize() = 0;

        virtual void Tick() = 0;
    };
}
