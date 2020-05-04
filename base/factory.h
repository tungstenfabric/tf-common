/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BASE__FACTORY_H__
#define __BASE__FACTORY_H__

// in Boost this macro defaults to 6 but we're defining FACTORY_TYPE_N8
// so we need to define it manually
#define BOOST_FUNCTIONAL_FORWARD_ADAPTER_MAX_ARITY 8

#include <boost/function.hpp>
#include <boost/functional/factory.hpp>
#include <boost/functional/forward_adapter.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/utility/enable_if.hpp>

#include "base/util.h"

template <class Derived>
class Factory {
  protected:
    static Derived *GetInstance() {
        if (singleton_ == NULL) {
            singleton_ = new Derived();
        }
        return singleton_;
    }
  private:
    static Derived *singleton_;
};

#include "base/factory_macros.h"

#define FACTORY_N0_STATIC_REGISTER(_Factory, _BaseType, _TypeImpl)\
static void _Factory ## _TypeImpl ## Register () {\
    _Factory::Register<_BaseType>(boost::factory<_TypeImpl *>());\
}\
MODULE_INITIALIZER(_Factory ## _TypeImpl ## Register)

#define FACTORY_STATIC_REGISTER(_Factory, _BaseType, _TypeImpl)\
static void _Factory ## _TypeImpl ## Register () {\
    _Factory::Register<_BaseType>(boost::forward_adapter<boost::factory<_TypeImpl *> >(boost::factory<_TypeImpl *>()));\
}\
MODULE_INITIALIZER(_Factory ## _TypeImpl ## Register)

#define FACTORY_PARAM_STATIC_REGISTER(_Factory, _BaseType, _Param, _TypeImpl)\
static void _Factory ## _TypeImpl ## Register () {\
    _Factory::Register<_BaseType, _Param>(boost::forward_adapter<boost::factory<_TypeImpl *> >(boost::factory<_TypeImpl *>()));\
}\
MODULE_INITIALIZER(_Factory ## _TypeImpl ## Register)

#endif
