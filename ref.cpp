#include <iostream>
#include "ref.hpp"

const ValueRef Constants::Null = ValueRef(std::make_shared<NullValue>());
const ValueRef Constants::True = ValueRef(std::make_shared<BooleanValue>(true));
const ValueRef Constants::False = ValueRef(std::make_shared<BooleanValue>(false));
