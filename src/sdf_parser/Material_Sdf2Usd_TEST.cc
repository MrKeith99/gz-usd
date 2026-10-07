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

#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include <gz/common/Filesystem.hh>
#include <gz/common/Material.hh>
#include <gz/common/Util.hh>

// TODO(ahcorde) this is to remove deprecated "warnings" in usd, these warnings
// are reported using #pragma message so normal diagnostic flags cannot remove
// them. This workaround requires this block to be used whenever usd is
// included.
#pragma push_macro ("__DEPRECATED")
#undef __DEPRECATED
#include <pxr/usd/usdShade/connectableAPI.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usd/stage.h>
#pragma pop_macro ("__DEPRECATED")

#include "Material.hh"

#include "gz/usd/UsdError.hh"
#include "gz/usd/sdf_parser/World.hh"
#include "sdf/Material.hh"
#include "sdf/Pbr.hh"
#include "sdf/Root.hh"
#include "test_config.h"
#include "test_utils.hh"
#include "../UsdTestUtils.hh"
#include "../Conversions.hh"

using namespace gz;

void CheckMaterial(
  const pxr::UsdPrim &_prim,
  const pxr::GfVec3f &_diffuseColor,
  const pxr::GfVec3f &_emissiveColor,
  bool _hasPbr,
  const std::string &_albedoName = "",
  const std::string &_normalName = "",
  const std::string &_roughnessName = "",
  const std::string &_metallicName = "")
{
  auto variantshader = pxr::UsdShadeShader(_prim);
  ASSERT_TRUE(variantshader);

  std::vector<pxr::UsdShadeInput> inputs = variantshader.GetInputs();
  pxr::GfVec3f diffuseColor {0, 0, 0};
  pxr::GfVec3f emissiveColor {0, 0, 0};

  // PBR-specific values
  const float _metallicConstant = 0;
  const bool _enableEmission = true;

  bool checkedDiffuse = false;
  bool checkedEmissive = false;
  bool checkedAlbedo = false;
  bool checkedNormal = false;
  bool checkedRoughness = false;
  bool checkedMetallicName = false;
  bool checkedMetallicConstant = false;
  bool checkedEnableEmission = false;
  for (auto &input : inputs)
  {
    if (_hasPbr && input.GetBaseName() == "diffuse_texture")
    {
      pxr::SdfAssetPath materialPathUSD;
      pxr::UsdShadeInput diffuseTextureShaderInput =
        variantshader.GetInput(pxr::TfToken("diffuse_texture"));
      diffuseTextureShaderInput.Get(&materialPathUSD);
      EXPECT_EQ(_albedoName,
        materialPathUSD.GetAssetPath());
      checkedAlbedo = true;
    }
    else if (_hasPbr && input.GetBaseName() == "metallic_constant")
    {
      pxr::UsdShadeInput metallicConstantShaderInput =
        variantshader.GetInput(pxr::TfToken("metallic_constant"));
      float metallicConstant;
      metallicConstantShaderInput.Get(&metallicConstant);
      EXPECT_FLOAT_EQ(_metallicConstant, metallicConstant);
      checkedMetallicConstant = true;
    }
    else if (input.GetBaseName() == "enable_emission")
    {
      bool enableEmission;
      pxr::UsdShadeInput enableEmissiveShaderInput =
        variantshader.GetInput(pxr::TfToken("enable_emission"));
      enableEmissiveShaderInput.Get(&enableEmission);
      EXPECT_EQ(_enableEmission, enableEmission);
      checkedEnableEmission = true;
    }
    else if (_hasPbr && input.GetBaseName() == "normalmap_texture")
    {
      pxr::SdfAssetPath materialPath;
      pxr::UsdShadeInput normalTextureShaderInput =
        variantshader.GetInput(pxr::TfToken("normalmap_texture"));
      normalTextureShaderInput.Get(&materialPath);
      EXPECT_EQ(_normalName, materialPath.GetAssetPath());
      checkedNormal = true;
    }
    else if (_hasPbr && input.GetBaseName() == "reflectionroughness_texture")
    {
      pxr::SdfAssetPath materialPath;
      pxr::UsdShadeInput roughnessTextureShaderInput =
        variantshader.GetInput(pxr::TfToken("reflectionroughness_texture"));
      roughnessTextureShaderInput.Get(&materialPath);
      EXPECT_EQ(_roughnessName, materialPath.GetAssetPath());
      checkedRoughness = true;
    }
    else if (_hasPbr && input.GetBaseName() == "metallic_texture")
    {
      pxr::SdfAssetPath materialPath;
      pxr::UsdShadeInput metallicTextureShaderInput =
        variantshader.GetInput(pxr::TfToken("metallic_texture"));
      metallicTextureShaderInput.Get(&materialPath);
      EXPECT_EQ(_metallicName, materialPath.GetAssetPath());
      checkedMetallicName = true;
    }
    else if (input.GetBaseName() == "emissive_color")
    {
      pxr::UsdShadeInput emissiveColorShaderInput =
        variantshader.GetInput(pxr::TfToken("emissive_color"));
      if (emissiveColorShaderInput.Get(&emissiveColor))
      {
        EXPECT_EQ(_emissiveColor, emissiveColor);
      }
      checkedEmissive = true;
    }
    else if (input.GetBaseName() == "diffuse_color_constant")
    {
      auto sourceInfoV = input.GetConnectedSources();
      if (sourceInfoV.size() > 0)
      {
        pxr::UsdShadeInput connectedInput =
          sourceInfoV[0].source.GetInput(sourceInfoV[0].sourceName);

        const pxr::SdfPath& thisAttrPath = connectedInput.GetAttr().GetPath();
        auto connectedPrim = _prim.GetStage()->GetPrimAtPath(
          thisAttrPath.GetPrimPath());
        if (connectedPrim)
        {
          connectedPrim.GetAttribute(
            pxr::TfToken("inputs:diffuse_color_constant")).Get(&diffuseColor);
        }
      }
      else
      {
        pxr::UsdShadeInput diffuseShaderInput =
          variantshader.GetInput(pxr::TfToken("diffuse_color_constant"));
        diffuseShaderInput.Get(&diffuseColor);
      }
      EXPECT_EQ(_diffuseColor, diffuseColor);
      checkedDiffuse = true;
    }
  }
  EXPECT_TRUE(checkedDiffuse);
  EXPECT_TRUE(checkedEmissive);
  EXPECT_TRUE(checkedEnableEmission);
  EXPECT_EQ(_hasPbr, checkedAlbedo);
  EXPECT_EQ(_hasPbr, checkedNormal);
  EXPECT_EQ(_hasPbr, checkedRoughness);
  EXPECT_EQ(_hasPbr, checkedMetallicName);
  EXPECT_EQ(_hasPbr, checkedMetallicConstant);
}

/////////////////////////////////////////////////
// Fixture that creates a USD stage for each test case.
class UsdStageFixture : public::testing::Test
{
  public: UsdStageFixture() = default;

  protected: void SetUp() override
  {
    this->stage = pxr::UsdStage::CreateInMemory();
    ASSERT_TRUE(this->stage);
  }

  public: pxr::UsdStageRefPtr stage;
};

TEST_F(UsdStageFixture, MaterialTextureName)
{
  gz::testing::ScopeExit removeCopiedMaterials(
      []
      {
        gz::common::removeAll(
          gz::common::joinPaths(gz::common::cwd(), "materials"));
      });

  sdf::setFindCallback(gz::usd::testing::findFileCb);
  gz::common::addFindFileURICallback(
    std::bind(&gz::usd::testing::FindResourceUri, std::placeholders::_1));
  const auto path = gz::testing::TestFile("sdf", "basic_shapes.sdf");

  gz::common::Material materialCommon;
  materialCommon.SetTextureImage("materials/textures/albedo_map.png");

  const sdf::Material materialSdf = gz::usd::convert(&materialCommon);

  const auto materialPathStr = std::string("/Looks/Material_0");
  auto materialPath = pxr::SdfPath(materialPathStr);

  gz::usd::UsdErrors errors = gz::usd::ParseSdfMaterial(
    &materialSdf, stage, materialPath);
  EXPECT_TRUE(errors.empty());

  {
    ASSERT_TRUE(this->stage->GetPrimAtPath(materialPath));

    const std::string materialshaderPath = materialPathStr + "/Shader";
    const auto materialShaderPrim = this->stage->GetPrimAtPath(
      pxr::SdfPath(materialshaderPath));
    ASSERT_TRUE(materialShaderPrim);

    auto variantshader = pxr::UsdShadeShader(materialShaderPrim);
    ASSERT_TRUE(variantshader);

    std::vector<pxr::UsdShadeInput> inputs = variantshader.GetInputs();

    bool checkedTextureInput = false;
    for (const auto &input : inputs)
    {
      if (input.GetBaseName() == "diffuse_texture")
      {
        pxr::SdfAssetPath materialPathUSD;
        pxr::UsdShadeInput diffuseTextureShaderInput =
          variantshader.GetInput(pxr::TfToken("diffuse_texture"));
        diffuseTextureShaderInput.Get(&materialPathUSD);
        EXPECT_EQ("materials/textures/albedo_map.png",
          materialPathUSD.GetAssetPath());
        checkedTextureInput = true;
        break;
      }
    }
    EXPECT_TRUE(checkedTextureInput);
  }
}

/////////////////////////////////////////////////
TEST_F(UsdStageFixture, Material)
{
  gz::testing::ScopeExit removeCopiedMaterials(
      []
      {
        gz::common::removeAll(
          gz::common::joinPaths(gz::common::cwd(), "materials"));
      });

  sdf::setFindCallback(gz::usd::testing::findFileCb);
  gz::common::addFindFileURICallback(
    std::bind(&gz::usd::testing::FindResourceUri, std::placeholders::_1));

  const auto path = gz::testing::TestFile("sdf", "basic_shapes.sdf");
  sdf::Root root;

  ASSERT_TRUE(gz::testing::LoadSdfFile(path, root));
  ASSERT_EQ(1u, root.WorldCount());
  auto world = root.WorldByIndex(0u);

  const auto worldPath = std::string("/" + world->Name());
  auto usdErrors = gz::usd::ParseSdfWorld(*world, stage, worldPath);
  EXPECT_TRUE(usdErrors.empty());

  auto worldPrim = this->stage->GetPrimAtPath(pxr::SdfPath(worldPath));
  ASSERT_TRUE(worldPrim);

  const std::string meshGeometryPath = worldPath + "/mesh/link/visual/geometry";
  ASSERT_TRUE(this->stage->GetPrimAtPath(pxr::SdfPath(meshGeometryPath)));
  const auto boundMesh = this->stage->GetPrimAtPath(
    pxr::SdfPath(meshGeometryPath + "/Cube"));
  ASSERT_TRUE(boundMesh);
  EXPECT_TRUE(boundMesh.HasAPI<pxr::UsdShadeMaterialBindingAPI>());

  {
    const std::string materialPath = worldPath + "/Looks/Material_3";
    ASSERT_TRUE(this->stage->GetPrimAtPath(pxr::SdfPath(materialPath)));

    const std::string materialshaderPath = materialPath + "/Shader";
    const auto materialShaderPrim = this->stage->GetPrimAtPath(
      pxr::SdfPath(materialshaderPath));
    ASSERT_TRUE(materialShaderPrim);

    CheckMaterial(materialShaderPrim,
      pxr::GfVec3f(0.2, 0.5, 0.1),
      pxr::GfVec3f(0, 0, 0),
      true,
      "materials/textures/albedo_map.png",
      "materials/textures/normal_map.png",
      "materials/textures/roughness_map.png",
      "materials/textures/metalness_map.png");
  }

  {
    const std::string materialPath = worldPath + "/Looks/Material_1";
    ASSERT_TRUE(this->stage->GetPrimAtPath(pxr::SdfPath(materialPath)));

    const std::string materialshaderPath = materialPath + "/Shader";
    const auto materialShaderPrim = this->stage->GetPrimAtPath(
      pxr::SdfPath(materialshaderPath));
    ASSERT_TRUE(materialShaderPrim);

    CheckMaterial(materialShaderPrim,
      pxr::GfVec3f(0, 0.1, 0.2),
      pxr::GfVec3f(0.12, 0.23, 0.34),
      false);
  }
}

/////////////////////////////////////////////////
TEST_F(UsdStageFixture, PreviewSurfaceAndOutputDir)
{
  const auto outputDir =
    gz::common::joinPaths(gz::common::cwd(), "material_output_dir_test");
  gz::testing::ScopeExit cleanup(
      [&outputDir]
      {
        gz::usd::SetTextureOutputDir("");
        gz::common::removeAll(outputDir);
      });
  gz::common::createDirectories(outputDir);
  gz::usd::SetTextureOutputDir(outputDir);

  // a texture that lives outside of the output directory
  const auto texture = gz::common::joinPaths(outputDir, "source_albedo.png");
  {
    std::ofstream file(texture);
    file << "not a real png";
  }
  const auto copiedTexture = gz::common::joinPaths(
    outputDir, "materials", "textures", "source_albedo.png");
  EXPECT_FALSE(gz::common::exists(copiedTexture));

  sdf::Material materialSdf;
  materialSdf.SetDiffuse(gz::math::Color(0.2f, 0.4f, 0.6f, 0.5f));
  sdf::Pbr pbr;
  sdf::PbrWorkflow workflow;
  workflow.SetType(sdf::PbrWorkflowType::METAL);
  workflow.SetAlbedoMap(texture);
  workflow.SetRoughness(0.3);
  workflow.SetMetalness(0.7);
  pbr.SetWorkflow(sdf::PbrWorkflowType::METAL, workflow);
  materialSdf.SetPbrMaterial(pbr);

  pxr::SdfPath materialPath;
  EXPECT_TRUE(gz::usd::ParseSdfMaterial(
    &materialSdf, stage, materialPath).empty());

  // the file goes next to the output, the asset path is relative to it
  EXPECT_TRUE(gz::common::exists(copiedTexture));
  EXPECT_EQ(outputDir, gz::usd::TextureOutputDir());

  const auto preview = pxr::UsdShadeShader(this->stage->GetPrimAtPath(
    materialPath.AppendChild(pxr::TfToken("PreviewSurface"))));
  ASSERT_TRUE(preview);
  pxr::TfToken id;
  EXPECT_TRUE(preview.GetIdAttr().Get(&id));
  EXPECT_EQ(pxr::TfToken("UsdPreviewSurface"), id);

  float value;
  EXPECT_TRUE(preview.GetInput(pxr::TfToken("roughness")).Get(&value));
  EXPECT_FLOAT_EQ(0.3f, value);
  EXPECT_TRUE(preview.GetInput(pxr::TfToken("metallic")).Get(&value));
  EXPECT_FLOAT_EQ(0.7f, value);
  EXPECT_TRUE(preview.GetInput(pxr::TfToken("opacity")).Get(&value));
  EXPECT_FLOAT_EQ(0.5f, value);

  // diffuseColor is driven by the UV texture that reads the copied file
  auto diffuse = preview.GetInput(pxr::TfToken("diffuseColor"));
  ASSERT_TRUE(diffuse);
  pxr::UsdShadeConnectableAPI source;
  pxr::TfToken sourceName;
  pxr::UsdShadeAttributeType sourceType;
  ASSERT_TRUE(pxr::UsdShadeConnectableAPI::GetConnectedSource(
    diffuse.GetAttr(), &source, &sourceName, &sourceType));
  EXPECT_EQ(pxr::TfToken("rgb"), sourceName);
  const auto uvTexture = pxr::UsdShadeShader(source.GetPrim());
  ASSERT_TRUE(uvTexture);
  EXPECT_TRUE(uvTexture.GetIdAttr().Get(&id));
  EXPECT_EQ(pxr::TfToken("UsdUVTexture"), id);
  pxr::SdfAssetPath file;
  EXPECT_TRUE(uvTexture.GetInput(pxr::TfToken("file")).Get(&file));
  EXPECT_EQ("materials/textures/source_albedo.png", file.GetAssetPath());

  // the universal surface output is connected next to the mdl one
  pxr::UsdShadeMaterial material(this->stage->GetPrimAtPath(materialPath));
  ASSERT_TRUE(material);
  EXPECT_TRUE(material.GetSurfaceOutput().HasConnectedSource());
  EXPECT_TRUE(material.GetSurfaceOutput(pxr::TfToken("mdl"))
      .HasConnectedSource());

  // without a texture, diffuseColor is a constant
  sdf::Material plainSdf;
  plainSdf.SetDiffuse(gz::math::Color(0.1f, 0.2f, 0.3f, 1.0f));
  pxr::SdfPath plainPath;
  EXPECT_TRUE(gz::usd::ParseSdfMaterial(
    &plainSdf, stage, plainPath).empty());
  const auto plainPreview = pxr::UsdShadeShader(this->stage->GetPrimAtPath(
    plainPath.AppendChild(pxr::TfToken("PreviewSurface"))));
  ASSERT_TRUE(plainPreview);
  pxr::GfVec3f color;
  EXPECT_TRUE(plainPreview.GetInput(pxr::TfToken("diffuseColor")).Get(&color));
  EXPECT_EQ(pxr::GfVec3f(0.1f, 0.2f, 0.3f), color);
}

/////////////////////////////////////////////////
TEST_F(UsdStageFixture, DefaultMetalnessIsZero)
{
  sdf::Material materialSdf;
  sdf::Pbr pbr;
  sdf::PbrWorkflow workflow;
  workflow.SetType(sdf::PbrWorkflowType::METAL);
  pbr.SetWorkflow(sdf::PbrWorkflowType::METAL, workflow);
  materialSdf.SetPbrMaterial(pbr);

  pxr::SdfPath path;
  EXPECT_TRUE(gz::usd::ParseSdfMaterial(&materialSdf, stage, path).empty());

  float value = -1;
  const auto preview = pxr::UsdShadeShader(this->stage->GetPrimAtPath(
    path.AppendChild(pxr::TfToken("PreviewSurface"))));
  EXPECT_TRUE(preview.GetInput(pxr::TfToken("metallic")).Get(&value));
  EXPECT_FLOAT_EQ(0.0f, value);

  const auto mdl = pxr::UsdShadeShader(this->stage->GetPrimAtPath(
    path.AppendChild(pxr::TfToken("Shader"))));
  value = -1;
  EXPECT_TRUE(mdl.GetInput(pxr::TfToken("metallic_constant")).Get(&value));
  EXPECT_FLOAT_EQ(0.0f, value);

  // without a pbr block there is no metallic input in the MDL shader
  sdf::Material plain;
  pxr::SdfPath plainPath;
  EXPECT_TRUE(gz::usd::ParseSdfMaterial(&plain, stage, plainPath).empty());
  const auto plainPreview = pxr::UsdShadeShader(this->stage->GetPrimAtPath(
    plainPath.AppendChild(pxr::TfToken("PreviewSurface"))));
  EXPECT_TRUE(plainPreview.GetInput(pxr::TfToken("metallic")).Get(&value));
  EXPECT_FLOAT_EQ(0.0f, value);
}

/////////////////////////////////////////////////
TEST_F(UsdStageFixture, NormalMapAndEmbeddedNames)
{
  const auto outputDir =
    gz::common::joinPaths(gz::common::cwd(), "material_normal_dir_test");
  gz::testing::ScopeExit cleanup(
      [&outputDir]
      {
        gz::usd::SetTextureOutputDir("");
        gz::common::removeAll(outputDir);
      });
  gz::common::createDirectories(outputDir);
  gz::usd::SetTextureOutputDir(outputDir);

  const auto normal = gz::common::joinPaths(outputDir, "mesh_0_normal.png");
  {
    std::ofstream file(normal);
    file << "not a real png";
  }

  sdf::Material materialSdf;
  sdf::Pbr pbr;
  sdf::PbrWorkflow workflow;
  workflow.SetType(sdf::PbrWorkflowType::METAL);
  workflow.SetNormalMap(normal, sdf::NormalMapSpace::TANGENT);
  // synthetic names of maps embedded in meshes are not looked up
  workflow.SetRoughnessMap("model.glb#*0_Roughness");
  pbr.SetWorkflow(sdf::PbrWorkflowType::METAL, workflow);
  materialSdf.SetPbrMaterial(pbr);

  pxr::SdfPath path;
  EXPECT_TRUE(gz::usd::ParseSdfMaterial(&materialSdf, stage, path).empty());

  const auto preview = pxr::UsdShadeShader(this->stage->GetPrimAtPath(
    path.AppendChild(pxr::TfToken("PreviewSurface"))));
  auto normalInput = preview.GetInput(pxr::TfToken("normal"));
  ASSERT_TRUE(normalInput);
  pxr::UsdShadeConnectableAPI source;
  pxr::TfToken sourceName;
  pxr::UsdShadeAttributeType sourceType;
  ASSERT_TRUE(pxr::UsdShadeConnectableAPI::GetConnectedSource(
    normalInput.GetAttr(), &source, &sourceName, &sourceType));
  pxr::SdfAssetPath file;
  EXPECT_TRUE(pxr::UsdShadeShader(source.GetPrim())
      .GetInput(pxr::TfToken("file")).Get(&file));
  EXPECT_EQ("materials/textures/mesh_0_normal.png", file.GetAssetPath());

  const auto mdl = pxr::UsdShadeShader(this->stage->GetPrimAtPath(
    path.AppendChild(pxr::TfToken("Shader"))));
  EXPECT_TRUE(mdl.GetInput(pxr::TfToken("normalmap_texture")).Get(&file));
  EXPECT_EQ("materials/textures/mesh_0_normal.png", file.GetAssetPath());
  EXPECT_FALSE(mdl.GetInput(pxr::TfToken("reflectionroughness_texture")));
}
