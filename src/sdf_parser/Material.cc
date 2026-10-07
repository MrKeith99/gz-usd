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

#include "Material.hh"

#include <map>
#include <string>

#include <gz/common/Filesystem.hh>
#include <gz/common/Util.hh>
#include <gz/common/URI.hh>

#include <gz/math/Color.hh>

// TODO(ahcorde) this is to remove deprecated "warnings" in usd, these warnings
// are reported using #pragma message so normal diagnostic flags cannot remove
// them. This workaround requires this block to be used whenever usd is
// included.
#pragma push_macro ("__DEPRECATED")
#undef __DEPRECATED
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#pragma pop_macro ("__DEPRECATED")

#include "sdf/Pbr.hh"
#include "../UsdUtils.hh"

namespace gz
{
// Inline bracket to help doxygen filtering.
inline namespace GZ_USD_VERSION_NAMESPACE {
//
namespace usd
{
  //////////////////////////////////////////////////
  /// \brief Directory that contains the USD stage being written
  static std::string &TextureOutputDirRef()
  {
    static std::string dir;
    return dir;
  }

  //////////////////////////////////////////////////
  void SetTextureOutputDir(const std::string &_dir)
  {
    TextureOutputDirRef() = _dir;
  }

  //////////////////////////////////////////////////
  const std::string &TextureOutputDir()
  {
    return TextureOutputDirRef();
  }

  //////////////////////////////////////////////////
  std::string TextureFilePath(const std::string &_name)
  {
    return gz::common::joinPaths(
      TextureOutputDir(), "materials", "textures", _name);
  }

  //////////////////////////////////////////////////
  /// \brief Copy a file to a destination, creating the directories
  /// \param[in] _path destination of the copy
  /// \param[in] _fullPath name of the file to copy
  /// \return True if the file at _fullPath is at _path. False otherwise
  bool copyMaterial(const std::string &_path, const std::string &_fullPath)
  {
    if (_path.empty() || _fullPath.empty())
      return false;
    if (_path == _fullPath)
      return true;

    const auto filePath = gz::common::parentPath(_path);
    if (!filePath.empty())
      gz::common::createDirectories(filePath);
    return gz::common::copyFile(_fullPath, _path);
  }

  //////////////////////////////////////////////////
  /// \brief Check that a map names a file. Maps embedded in meshes have a
  /// synthetic name containing '#' and no file.
  /// \param[in] _texture Texture path or URI as written in the SDF
  /// \return True if _texture can be looked up as a file
  bool IsExportableTexture(const std::string &_texture)
  {
    return !_texture.empty() && _texture.find('#') == std::string::npos;
  }

  //////////////////////////////////////////////////
  /// \brief Copy a texture next to the output stage
  /// \param[in] _material Material that owns the texture
  /// \param[in] _texture Texture path or URI as written in the SDF
  /// \return The texture path relative to the stage:
  /// materials/textures/<filename with extension>
  std::string CopyTexture(const sdf::Material &_material,
      const std::string &_texture)
  {
    std::string fullName = gz::common::findFile(_texture);
    if (fullName.empty())
      fullName = gz::common::findFile(gz::common::basename(_texture));
    if (fullName.empty() && !_material.FilePath().empty())
    {
      fullName = gz::common::joinPaths(
        gz::common::parentPath(_material.FilePath()), _texture);
    }
    if (fullName.empty() || !gz::common::exists(fullName))
      fullName = _texture;

    const auto name = gz::common::basename(_texture);
    copyMaterial(TextureFilePath(name), fullName);
    return gz::common::joinPaths("materials", "textures", name);
  }

  /// \brief Fill Material shader attributes and properties
  /// \param[in] _prim USD primitive
  /// \param[in] _name Name of the field attribute or property
  /// \param[in] _vType Type of the field
  /// \param[in] _value Value of the field
  /// \param[in] _customData Custom data to set the field
  /// \param[in] _displayName Display name
  /// \param[in] _displayGroup Display group
  /// \param[in] _doc Documentation of the field
  /// \param[in] _colorSpace if the material is a texture, we can specify the
  /// color space of the image
  /// \return gz::usd::UsdErrors, which is a list of UsdError objects.
  /// This list is empty if no errors occurred when creating the material input.
  template<typename T>
  gz::usd::UsdErrors CreateMaterialInput(
    const pxr::UsdPrim &_prim,
    const std::string &_name,
    const pxr::SdfValueTypeName &_vType,
    const T &_value,
    const std::map<pxr::TfToken, pxr::VtValue> &_customData,
    const pxr::TfToken &_displayName,
    const pxr::TfToken &_displayGroup,
    const std::string &_doc,
    const pxr::TfToken &_colorSpace = pxr::TfToken(""))
  {
    gz::usd::UsdErrors errors;
    auto shader = pxr::UsdShadeShader(_prim);
    if (!shader)
    {
      errors.emplace_back(UsdError(
        gz::usd::UsdErrorCode::INVALID_PRIM_PATH,
        "Unable to convert the prim to a UsdShadeShader"));
      return errors;
    }

    auto existingInput = shader.GetInput(pxr::TfToken(_name));
    pxr::SdfValueTypeName vTypeName;
    if (_vType.IsScalar())
    {
      vTypeName = _vType.GetScalarType();
    }
    else if (_vType.IsArray())
    {
      vTypeName = _vType.GetArrayType();
    }
    auto surfaceInput = shader.CreateInput(
      pxr::TfToken(_name), vTypeName);
    surfaceInput.Set(_value);
    auto attr = surfaceInput.GetAttr();

    for (const auto &[key, customValue] : _customData)
    {
      attr.SetCustomDataByKey(key, customValue);
    }
    if (!_displayName.GetString().empty())
    {
      attr.SetDisplayName(_displayName);
    }
    if (!_displayGroup.GetString().empty())
    {
      attr.SetDisplayGroup(_displayGroup);
    }
    if (!_doc.empty())
    {
      attr.SetDocumentation(_doc);
    }
    if (!_colorSpace.GetString().empty())
    {
      attr.SetColorSpace(_colorSpace);
    }
    return errors;
  }

  //////////////////////////////////////////////////
  /// \brief Add a UsdPreviewSurface network to a material so that renderers
  /// that do not support MDL (e.g., usdview, Newton) can display it.
  /// \param[in] _stage The stage that contains _material
  /// \param[in] _material The USD material to extend
  /// \param[in] _sdf The SDF material
  /// \param[in] _albedo Albedo texture path relative to the stage. Empty if
  /// the material has no texture.
  /// \param[in] _normal Normal map path relative to the stage. Empty if the
  /// material has none.
  /// \param[in] _roughness Roughness of the material
  /// \param[in] _metallic Metalness of the material
  void CreatePreviewSurface(pxr::UsdStageRefPtr &_stage,
      pxr::UsdShadeMaterial &_material, const sdf::Material &_sdf,
      const std::string &_albedo, const std::string &_normal,
      float _roughness, float _metallic)
  {
    const auto materialPath = _material.GetPath();
    auto preview = pxr::UsdShadeShader::Define(
      _stage, materialPath.AppendChild(pxr::TfToken("PreviewSurface")));
    preview.CreateIdAttr().Set(pxr::TfToken("UsdPreviewSurface"));

    const auto diffuse = _sdf.Diffuse();
    auto diffuseInput = preview.CreateInput(
      pxr::TfToken("diffuseColor"), pxr::SdfValueTypeNames->Color3f);
    pxr::UsdShadeOutput readerOut;
    if (!_albedo.empty() || !_normal.empty())
    {
      auto reader = pxr::UsdShadeShader::Define(
        _stage, materialPath.AppendChild(pxr::TfToken("stReader")));
      reader.CreateIdAttr().Set(pxr::TfToken("UsdPrimvarReader_float2"));
      reader.CreateInput(pxr::TfToken("varname"),
          pxr::SdfValueTypeNames->String).Set(std::string("st"));
      readerOut = reader.CreateOutput(
        pxr::TfToken("result"), pxr::SdfValueTypeNames->Float2);
    }

    // Create a UsdUVTexture reading _file and return its rgb output
    auto createTexture = [&](const std::string &_name,
        const std::string &_file, const std::string &_colorSpace)
    {
      auto texture = pxr::UsdShadeShader::Define(
        _stage, materialPath.AppendChild(pxr::TfToken(_name)));
      texture.CreateIdAttr().Set(pxr::TfToken("UsdUVTexture"));
      texture.CreateInput(pxr::TfToken("file"),
          pxr::SdfValueTypeNames->Asset).Set(pxr::SdfAssetPath(_file));
      texture.CreateInput(pxr::TfToken("st"),
          pxr::SdfValueTypeNames->Float2).ConnectToSource(readerOut);
      texture.CreateInput(pxr::TfToken("wrapS"),
          pxr::SdfValueTypeNames->Token).Set(pxr::TfToken("repeat"));
      texture.CreateInput(pxr::TfToken("wrapT"),
          pxr::SdfValueTypeNames->Token).Set(pxr::TfToken("repeat"));
      texture.CreateInput(pxr::TfToken("sourceColorSpace"),
          pxr::SdfValueTypeNames->Token).Set(pxr::TfToken(_colorSpace));
      return texture;
    };

    if (_albedo.empty())
    {
      diffuseInput.Set(pxr::GfVec3f(diffuse.R(), diffuse.G(), diffuse.B()));
    }
    else
    {
      auto texture = createTexture("AlbedoTexture", _albedo, "sRGB");
      diffuseInput.ConnectToSource(texture.CreateOutput(
        pxr::TfToken("rgb"), pxr::SdfValueTypeNames->Float3));
    }

    if (!_normal.empty())
    {
      auto texture = createTexture("NormalTexture", _normal, "raw");
      // tangent-space normals are stored in [0, 1]
      texture.CreateInput(pxr::TfToken("scale"),
          pxr::SdfValueTypeNames->Float4).Set(pxr::GfVec4f(2, 2, 2, 1));
      texture.CreateInput(pxr::TfToken("bias"),
          pxr::SdfValueTypeNames->Float4).Set(pxr::GfVec4f(-1, -1, -1, 0));
      preview.CreateInput(pxr::TfToken("normal"),
          pxr::SdfValueTypeNames->Normal3f).ConnectToSource(
          texture.CreateOutput(
            pxr::TfToken("rgb"), pxr::SdfValueTypeNames->Float3));
    }

    preview.CreateInput(pxr::TfToken("roughness"),
        pxr::SdfValueTypeNames->Float).Set(_roughness);
    preview.CreateInput(pxr::TfToken("metallic"),
        pxr::SdfValueTypeNames->Float).Set(_metallic);
    preview.CreateInput(pxr::TfToken("opacity"),
        pxr::SdfValueTypeNames->Float).Set(static_cast<float>(diffuse.A()));

    const auto emissive = _sdf.Emissive();
    if (emissive.A() > 0)
    {
      preview.CreateInput(pxr::TfToken("emissiveColor"),
          pxr::SdfValueTypeNames->Color3f).Set(
          pxr::GfVec3f(emissive.R(), emissive.G(), emissive.B()));
    }

    _material.CreateSurfaceOutput().ConnectToSource(preview.CreateOutput(
      pxr::TfToken("surface"), pxr::SdfValueTypeNames->Token));
  }

  gz::usd::UsdErrors ParseSdfMaterial(const sdf::Material *_materialSdf,
      pxr::UsdStageRefPtr &_stage, pxr::SdfPath &_materialPath)
  {
    gz::usd::UsdErrors errors;

    const auto looksPath = LooksPath(_stage);
    auto looksPrim = _stage->GetPrimAtPath(looksPath);
    if (!looksPrim)
    {
      looksPrim = _stage->DefinePrim(looksPath, pxr::TfToken("Scope"));
    }

    // This variable will increase with every new material to avoid collision
    // with the names of the materials
    static int i = 0;

    _materialPath = looksPath.AppendChild(
      pxr::TfToken("Material_" + std::to_string(i)));
    i++;

    pxr::UsdShadeMaterial materialUsd;
    auto usdMaterialPrim = _stage->GetPrimAtPath(_materialPath);
    if (!usdMaterialPrim)
    {
      materialUsd = pxr::UsdShadeMaterial::Define(_stage, _materialPath);
    }
    else
    {
      materialUsd = pxr::UsdShadeMaterial(usdMaterialPrim);
    }

    const auto shaderPath = pxr::SdfPath(_materialPath.GetString() + "/Shader");
    auto usdShader = pxr::UsdShadeShader::Define(_stage, shaderPath);
    auto shaderPrim = _stage->GetPrimAtPath(shaderPath);
    if (!shaderPrim)
    {
      errors.emplace_back(UsdError(
        gz::usd::UsdErrorCode::INVALID_PRIM_PATH,
        "Not able to cast the UsdShadeShader at path [" + shaderPath.GetString()
        + "] to a Prim"));
      return errors;
    }

    auto shaderOut = pxr::UsdShadeConnectableAPI(shaderPrim).CreateOutput(
      pxr::TfToken("out"), pxr::SdfValueTypeNames->Token);
    const auto mdlToken = pxr::TfToken("mdl");
    materialUsd.CreateSurfaceOutput(mdlToken).ConnectToSource(shaderOut);
    materialUsd.CreateVolumeOutput(mdlToken).ConnectToSource(shaderOut);
    materialUsd.CreateDisplacementOutput(mdlToken).ConnectToSource(shaderOut);
    usdShader.GetImplementationSourceAttr().Set(
      pxr::UsdShadeTokens->sourceAsset);
    usdShader.SetSourceAsset(pxr::SdfAssetPath("OmniPBR.mdl"), mdlToken);
    usdShader.SetSourceAssetSubIdentifier(pxr::TfToken("OmniPBR"), mdlToken);

    const std::map<pxr::TfToken, pxr::VtValue> customDataDiffuse =
    {
      {pxr::TfToken("default"), pxr::VtValue(pxr::GfVec3f(0.2, 0.2, 0.2))},
      {pxr::TfToken("range:max"),
       pxr::VtValue(pxr::GfVec3f(100000, 100000, 100000))},
      {pxr::TfToken("range:min"), pxr::VtValue(pxr::GfVec3f(0, 0, 0))}
    };
    const math::Color diffuse = _materialSdf->Diffuse();

    auto errorsMaterialDiffuseColorConstant = CreateMaterialInput<pxr::GfVec3f>(
      shaderPrim,
      "diffuse_color_constant",
      pxr::SdfValueTypeNames->Color3f,
      pxr::GfVec3f(diffuse.R(), diffuse.G(), diffuse.B()),
      customDataDiffuse,
      pxr::TfToken("Base Color"),
      pxr::TfToken("Albedo"),
      "This is the base color");

    if (!errorsMaterialDiffuseColorConstant.empty())
    {
      errors.insert(
        errors.end(),
        errorsMaterialDiffuseColorConstant.begin(),
        errorsMaterialDiffuseColorConstant.end());
      errors.push_back(UsdError(UsdErrorCode::INVALID_MATERIAL,
            "Unable to set the base color of the material at path ["
            + _materialPath.GetString() + "]"));
      return errors;
    }

    const std::map<pxr::TfToken, pxr::VtValue> customDataEmissive =
    {
      {pxr::TfToken("default"), pxr::VtValue(pxr::GfVec3f(1, 0.1, 0.1))},
      {pxr::TfToken("range:max"),
       pxr::VtValue(pxr::GfVec3f(100000, 100000, 100000))},
      {pxr::TfToken("range:min"), pxr::VtValue(pxr::GfVec3f(0, 0, 0))}
    };
    math::Color emissive = _materialSdf->Emissive();
    auto errorsMaterialEmissiveColor = CreateMaterialInput<pxr::GfVec3f>(
      shaderPrim,
      "emissive_color",
      pxr::SdfValueTypeNames->Color3f,
      pxr::GfVec3f(emissive.R(), emissive.G(), emissive.B()),
      customDataEmissive,
      pxr::TfToken("Emissive Color"),
      pxr::TfToken("Emissive"),
      "The emission color");

    if (!errorsMaterialEmissiveColor.empty())
    {
      errors.insert(
        errors.end(),
        errorsMaterialEmissiveColor.begin(),
        errorsMaterialEmissiveColor.end());
      errors.push_back(UsdError(UsdErrorCode::INVALID_MATERIAL,
            "Unable to set the emission color of the material at path ["
            + _materialPath.GetString() + "]"));
      return errors;
    }

    const std::map<pxr::TfToken, pxr::VtValue> customDataEnableEmission =
    {
      {pxr::TfToken("default"), pxr::VtValue(0)}
    };

    auto errorsMaterialEnableEmission = CreateMaterialInput<bool>(
      shaderPrim,
      "enable_emission",
      pxr::SdfValueTypeNames->Bool,
      emissive.A() > 0,
      customDataEnableEmission,
      pxr::TfToken("Enable Emissive"),
      pxr::TfToken("Emissive"),
      "Enables the emission of light from the material");

    if (!errorsMaterialEnableEmission.empty())
    {
      errors.insert(
        errors.end(),
        errorsMaterialEnableEmission.begin(),
        errorsMaterialEnableEmission.end());
      errors.push_back(UsdError(UsdErrorCode::INVALID_MATERIAL,
            "Unable to set the emissive enabled propery of the material at path"
            " [" + _materialPath.GetString() + "]"));
      return errors;
    }

    const std::map<pxr::TfToken, pxr::VtValue> customDataIntensity =
    {
      {pxr::TfToken("default"), pxr::VtValue(40)},
      {pxr::TfToken("range:max"), pxr::VtValue(100000)},
      {pxr::TfToken("range:min"), pxr::VtValue(0)}
    };
    auto errorsMaterialEmissiveIntensity = CreateMaterialInput<float>(
      shaderPrim,
      "emissive_intensity",
      pxr::SdfValueTypeNames->Float,
      emissive.A(),
      customDataIntensity,
      pxr::TfToken("Emissive Intensity"),
      pxr::TfToken("Emissive"),
      "Intensity of the emission");

    if (!errorsMaterialEmissiveIntensity.empty())
    {
      errors.insert(
        errors.end(),
        errorsMaterialEmissiveIntensity.begin(),
        errorsMaterialEmissiveIntensity.end());
      errors.push_back(UsdError(UsdErrorCode::INVALID_MATERIAL,
            "Unable to set the emissive intensity of the material at path ["
            + _materialPath.GetString() + "]"));
      return errors;
    }

    std::string albedoPath;
    std::string normalPath;
    float roughness = 0.5f;
    float metallic = 0.0f;
    const sdf::Pbr * pbr = _materialSdf->PbrMaterial();
    if (pbr)
    {
      const sdf::PbrWorkflow * pbrWorkflow =
        pbr->Workflow(sdf::PbrWorkflowType::METAL);
      if (!pbrWorkflow)
      {
        pbrWorkflow = pbr->Workflow(sdf::PbrWorkflowType::SPECULAR);
      }

      if (pbrWorkflow)
      {
        // sdformat cannot tell an unset metalness from 0.5, and Gazebo
        // renders materials without metalness as non-metal.
        const double sdfMetalness = pbrWorkflow->Metalness();
        metallic = sdfMetalness == 0.5 ? 0.0f :
          static_cast<float>(sdfMetalness);
        roughness = static_cast<float>(pbrWorkflow->Roughness());
        const std::map<pxr::TfToken, pxr::VtValue> customDataMetallicConstant =
        {
          {pxr::TfToken("default"), pxr::VtValue(0.5)},
          {pxr::TfToken("range:max"), pxr::VtValue(1)},
          {pxr::TfToken("range:min"), pxr::VtValue(0)}
        };
        auto errorsMaterialMetallicConstant = CreateMaterialInput<float>(
          shaderPrim,
          "metallic_constant",
          pxr::SdfValueTypeNames->Float,
          metallic,
          customDataMetallicConstant,
          pxr::TfToken("Metallic Amount"),
          pxr::TfToken("Reflectivity"),
          "Metallic Material");
        if (!errorsMaterialMetallicConstant.empty())
        {
          errors.insert(
            errors.end(),
            errorsMaterialMetallicConstant.begin(),
            errorsMaterialMetallicConstant.end());
          errors.push_back(UsdError(UsdErrorCode::INVALID_MATERIAL,
            "Unable to set the metallic constant of the material at path ["
            + _materialPath.GetString() + "]"));
          return errors;
        }
        const std::map<pxr::TfToken, pxr::VtValue> customDataRoughnessConstant =
        {
          {pxr::TfToken("default"), pxr::VtValue(0.5)},
          {pxr::TfToken("range:max"), pxr::VtValue(1)},
          {pxr::TfToken("range:min"), pxr::VtValue(0)}
        };
        auto errorsMaterialReflectionRoughnessConstant =
          CreateMaterialInput<float>(
            shaderPrim,
            "reflection_roughness_constant",
            pxr::SdfValueTypeNames->Float,
            pbrWorkflow->Roughness(),
            customDataRoughnessConstant,
            pxr::TfToken("Roughness Amount"),
            pxr::TfToken("Reflectivity"),
            "Higher roughness values lead to more blurry reflections");
        if (!errorsMaterialReflectionRoughnessConstant.empty())
        {
          errors.insert(
            errors.end(),
            errorsMaterialReflectionRoughnessConstant.begin(),
            errorsMaterialReflectionRoughnessConstant.end());
          errors.push_back(UsdError(UsdErrorCode::INVALID_MATERIAL,
            "Unable to set the roughness constant of the material at path ["
            + _materialPath.GetString() + "]"));
          return errors;
        }

        const std::map<pxr::TfToken, pxr::VtValue> customDefaultSdfAssetPath =
        {
          {pxr::TfToken("default"), pxr::VtValue(pxr::SdfAssetPath())},
        };

        if (IsExportableTexture(pbrWorkflow->AlbedoMap()))
        {
          const std::string copyPath =
            CopyTexture(*_materialSdf, pbrWorkflow->AlbedoMap());
          albedoPath = copyPath;

          auto errorsMaterialDiffuseTexture =
            CreateMaterialInput<pxr::SdfAssetPath>(
              shaderPrim,
              "diffuse_texture",
              pxr::SdfValueTypeNames->Asset,
              pxr::SdfAssetPath(copyPath),
              customDefaultSdfAssetPath,
              pxr::TfToken("Base Map"),
              pxr::TfToken("Albedo"),
              "",
              pxr::TfToken("auto"));
          if (!errorsMaterialDiffuseTexture.empty())
          {
            errors.insert(
              errors.end(),
              errorsMaterialDiffuseTexture.begin(),
              errorsMaterialDiffuseTexture.end());
            errors.push_back(UsdError(UsdErrorCode::INVALID_MATERIAL,
              "Unable to set the albedo of the material at path ["
              + _materialPath.GetString() + "]"));
            return errors;
          }
        }
        if (IsExportableTexture(pbrWorkflow->MetalnessMap()))
        {
          const std::string copyPath =
            CopyTexture(*_materialSdf, pbrWorkflow->MetalnessMap());

          auto errorsMaterialMetallicTexture =
            CreateMaterialInput<pxr::SdfAssetPath>(
              shaderPrim,
              "metallic_texture",
              pxr::SdfValueTypeNames->Asset,
              pxr::SdfAssetPath(copyPath),
              customDefaultSdfAssetPath,
              pxr::TfToken("Metallic Map"),
              pxr::TfToken("Reflectivity"),
              "",
              pxr::TfToken("raw"));
          if (!errorsMaterialMetallicTexture.empty())
          {
            errors.insert(
              errors.end(),
              errorsMaterialMetallicTexture.begin(),
              errorsMaterialMetallicTexture.end());
            errors.push_back(UsdError(UsdErrorCode::INVALID_MATERIAL,
              "Unable to set the reflectivity of the material at path ["
              + _materialPath.GetString() + "]"));
            return errors;
          }
        }
        if (IsExportableTexture(pbrWorkflow->NormalMap()))
        {
          const std::string copyPath =
            CopyTexture(*_materialSdf, pbrWorkflow->NormalMap());
          normalPath = copyPath;

          auto errorsMaterialNormalMapTexture =
            CreateMaterialInput<pxr::SdfAssetPath>(
              shaderPrim,
              "normalmap_texture",
              pxr::SdfValueTypeNames->Asset,
              pxr::SdfAssetPath(copyPath),
              customDefaultSdfAssetPath,
              pxr::TfToken("Normal Map"),
              pxr::TfToken("Normal"),
              "",
              pxr::TfToken("raw"));
          if (!errorsMaterialNormalMapTexture.empty())
          {
            errors.insert(
              errors.end(),
              errorsMaterialNormalMapTexture.begin(),
              errorsMaterialNormalMapTexture.end());
            errors.push_back(UsdError(UsdErrorCode::INVALID_MATERIAL,
              "Unable to set the normal map of the material at path ["
              + _materialPath.GetString() + "]"));
            return errors;
          }
        }
        if (IsExportableTexture(pbrWorkflow->RoughnessMap()))
        {
          const std::string copyPath =
            CopyTexture(*_materialSdf, pbrWorkflow->RoughnessMap());

          auto errorsMaterialReflectionRoughnessTexture =
            CreateMaterialInput<pxr::SdfAssetPath>(
              shaderPrim,
              "reflectionroughness_texture",
              pxr::SdfValueTypeNames->Asset,
              pxr::SdfAssetPath(copyPath),
              customDefaultSdfAssetPath,
              pxr::TfToken("RoughnessMap Map"),
              pxr::TfToken("RoughnessMap"),
              "",
              pxr::TfToken("raw"));
          if (!errorsMaterialReflectionRoughnessTexture.empty())
          {
            errors.insert(
              errors.end(),
              errorsMaterialReflectionRoughnessTexture.begin(),
              errorsMaterialReflectionRoughnessTexture.end());
            errors.push_back(UsdError(UsdErrorCode::INVALID_MATERIAL,
              "Unable to set the roughness map of the material at path ["
              + _materialPath.GetString() + "]"));
            return errors;
          }

          const std::map<pxr::TfToken, pxr::VtValue>
            customDataRoughnessTextureInfluence =
          {
            {pxr::TfToken("default"), pxr::VtValue(0)},
            {pxr::TfToken("range:max"), pxr::VtValue(1)},
            {pxr::TfToken("range:min"), pxr::VtValue(0)}
          };

          auto errorsMaterialReflectionRoughnessTextureInfluence =
            CreateMaterialInput<bool>(
              shaderPrim,
              "reflection_roughness_texture_influence",
              pxr::SdfValueTypeNames->Bool,
              true,
              customDataRoughnessTextureInfluence,
              pxr::TfToken("Roughness Map Influence"),
              pxr::TfToken("Reflectivity"),
              "",
              pxr::TfToken("raw"));
          if (!errorsMaterialReflectionRoughnessTextureInfluence.empty())
          {
            errors.insert(
              errors.end(),
              errorsMaterialReflectionRoughnessTextureInfluence.begin(),
              errorsMaterialReflectionRoughnessTextureInfluence.end());
            errors.push_back(UsdError(UsdErrorCode::INVALID_MATERIAL,
              "Unable to set the reflectivity of the material at path ["
              + _materialPath.GetString() + "]"));
            return errors;
          }
        }
      }
    }

    CreatePreviewSurface(_stage, materialUsd, *_materialSdf, albedoPath,
        normalPath, roughness, metallic);

    return errors;
  }
}
}
}
