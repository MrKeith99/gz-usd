/*
 * Copyright 2022 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */


#ifndef GZ_USD_USD_CONVERSIONS_HH_
#define GZ_USD_USD_CONVERSIONS_HH_

#include <memory>

#include <gz/common/Material.hh>

#include "sdf/Material.hh"
#include <gz/usd/config.hh>

namespace gz
{
  // Inline bracket to help doxygen filtering.
  inline namespace GZ_USD_VERSION_NAMESPACE {
  //
  namespace usd
  {
    /// \brief Specialized conversion from an Ignition Common Material
    /// to a SDF material
    /// \param[in] _in Ignition Common Material.
    /// \return SDF material.
    sdf::Material convert(const common::Material *_in);

    /// \brief Specialized conversion from an SDF material to a Ignition Common
    /// material.
    /// \param[in] _in SDF material.
    /// \param[out] _out The Ignition Common Material.
    void convert(const sdf::Material &_in, common::Material &_out);
  }
  }
}

#endif
