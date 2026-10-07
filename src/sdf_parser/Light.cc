/*
 * Copyright (C) 2022 Open Source Robotics Foundation
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

#include "Light.hh"

#include <string>

#include <gz/math/Pose3.hh>
#include <gz/math/Quaternion.hh>
#include <gz/math/Vector3.hh>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usd/prim.h>
#pragma push_macro ("__DEPRECATED")
#undef __DEPRECATED
#include <pxr/usd/usd/stage.h>
#pragma pop_macro ("__DEPRECATED")

#include "sdf/Light.hh"
#include "../UsdUtils.hh"

namespace gz
{
// Inline bracke to help doxygen filtering.
inline namespace GZ_USD_VERSION_NAMESPACE {
//
namespace usd
{
  /// \brief Factor applied on top of the default light intensities
  static double gLightIntensityScale = 1.0;

  //////////////////////////////////////////////////
  void SetLightIntensityScale(double _scale)
  {
    gLightIntensityScale = _scale;
  }

  //////////////////////////////////////////////////
  float LightIntensity(const sdf::Light &_light)
  {
    // Isaac Sim's default sun is 1000 to 3000 and its sphere light is 30000
    const double base =
      _light.Type() == sdf::LightType::DIRECTIONAL ? 1000.0 : 30000.0;
    return static_cast<float>(base * _light.Intensity() * gLightIntensityScale);
  }

  gz::usd::UsdErrors ParseSdfLight(const sdf::Light &_light,
      pxr::UsdStageRefPtr &_stage, const std::string &_path)
  {
    const pxr::SdfPath sdfLightPath(_path);
    gz::usd::UsdErrors errors;
    switch (_light.Type())
    {
      case sdf::LightType::POINT:
        {
          auto pointLight =
            pxr::UsdLuxSphereLight::Define(_stage, sdfLightPath);
          pointLight.CreateTreatAsPointAttr().Set(true);
        }
        break;
      case sdf::LightType::SPOT:
        pxr::UsdLuxDiskLight::Define(_stage, sdfLightPath);
        break;
      case sdf::LightType::DIRECTIONAL:
        pxr::UsdLuxDistantLight::Define(_stage, sdfLightPath);
        break;
      case sdf::LightType::INVALID:
      default:
        errors.push_back(UsdError(sdf::Error(
            sdf::ErrorCode::ATTRIBUTE_INCORRECT_TYPE,
            "The light type that was given cannot be parsed to USD.")));
        return errors;
    }

    math::Pose3d pose;
    auto poseErrors = gz::usd::PoseWrtParent(_light, pose);
    if (!poseErrors.empty())
    {
      for (const auto &e : poseErrors)
        errors.push_back(e);
      return errors;
    }

    // USD lights emit along their local -Z axis, SDF along <direction>
    if (_light.Type() != sdf::LightType::POINT &&
        _light.Direction().Length() > 0)
    {
      pose.Rot() = pose.Rot() * LightDirectionRotation(_light.Direction());
    }

    poseErrors = gz::usd::SetPose(pose, _stage, sdfLightPath);
    if (!poseErrors.empty())
    {
      for (const auto &e : poseErrors)
        errors.push_back(e);
      errors.push_back(UsdError(UsdErrorCode::GZ_USD_TO_USD_PARSING_ERROR,
            "Unable to set the pose of the light prim corresponding to the "
            "SDF light named [" + _light.Name() + "]"));
      return errors;
    }

    // This is a workaround to set the light's intensity attribute. Using the
    // UsdLuxLightAPI sets the light's "inputs:intensity" attribute, but isaac
    // sim reads the light's "intensity" attribute. Both inputs:intensity and
    // intensity are set to provide flexibility with other USD renderers
    const float usdLightIntensity = LightIntensity(_light);
    auto lightPrim = _stage->GetPrimAtPath(sdfLightPath);
    lightPrim.CreateAttribute(pxr::TfToken("intensity"),
        pxr::SdfValueTypeNames->Float, false).Set(usdLightIntensity);
    auto lightAPI = pxr::UsdLuxLightAPI(lightPrim);
    lightAPI.CreateIntensityAttr().Set(usdLightIntensity);
    const auto diffuse = _light.Diffuse();
    lightAPI.CreateColorAttr().Set(pxr::GfVec3f(
      diffuse.R(), diffuse.G(), diffuse.B()));

    // TODO(adlarkin) Other things to look at (there may be more):
    // * exposure - I don't think SDF has this, but USD does. See the
    //    UsdLightAPI::GetExposureAttr method
    // * specular - USD takes it as a scalar multiplier, SDF as an RGB color

    return errors;
  }
}
}
}
