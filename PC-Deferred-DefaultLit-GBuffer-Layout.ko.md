# PC Deferred Default Lit GBuffer Layout

## 범위

- Desktop Deferred 경로 (`!SHADING_PATH_MOBILE`)
- Legacy GBuffer 경로 (`EncodeGBuffer`, non-Substrate)
- `Default Lit` 셰이딩 모델 (`SHADINGMODELID_DEFAULT_LIT = 1`)
- 대상 MRT: `GBufferA` ~ `GBufferE`

Substrate 경로를 쓰는 경우에는 레이아웃이 달라질 수 있으므로 이 문서와 다르게 봐야 한다.

## 요약

| MRT | 채널 의미 |
| --- | --- |
| `GBufferA` | `rgb = WorldNormal`, `a = PerObjectGBufferData` |
| `GBufferB` | `r = Metallic`, `g = Specular`, `b = Roughness`, `a = ShadingModelID + SelectiveOutputMask` |
| `GBufferC` | `rgb = BaseColor`, `a = GenericAO 슬롯` |
| `GBufferD` | `Default Lit`에서는 사용하지 않음, `0` |
| `GBufferE` | `rgba = PrecomputedShadowFactors` |

## GBufferA

| 채널 | 저장 값 | 디코드/해석 | 비고 |
| --- | --- | --- | --- |
| `A.r` | `EncodeNormal(WorldNormal).r` | `DecodeNormal(InGBufferA.xyz).x` | 현재 구현은 `N * 0.5 + 0.5` |
| `A.g` | `EncodeNormal(WorldNormal).g` | `DecodeNormal(InGBufferA.xyz).y` | 현재 구현은 `N * 0.5 + 0.5` |
| `A.b` | `EncodeNormal(WorldNormal).b` | `DecodeNormal(InGBufferA.xyz).z` | 현재 구현은 `N * 0.5 + 0.5` |
| `A.a` | `PerObjectGBufferData` | per-object 플래그 2비트 해석 | `CastContactShadow`, `HasDynamicIndirectShadowCasterRepresentation` 용도 |

### `GBufferA.a` 상세

`PerObjectGBufferData`는 실질적으로 2비트 정보가 들어간다.

| 비트 | 의미 | 소스 |
| --- | --- | --- |
| bit 0 | `CastContactShadow` | primitive flag `PRIMITIVE_SCENE_DATA_FLAG_HAS_CAST_CONTACT_SHADOW` |
| bit 1 | `CapsuleRepresentation` / `HasDynamicIndirectShadowCasterRepresentation` | primitive flag `PRIMITIVE_SCENE_DATA_FLAG_HAS_CAPSULE_REPRESENTATION` |

인코딩 값은 `(2 * CapsuleRepresentation + CastContactShadow) / 3` 이므로 실제 저장값은 `0`, `1/3`, `2/3`, `1` 중 하나다.

## GBufferB

| 채널 | 저장 값 | 디코드/해석 | 비고 |
| --- | --- | --- | --- |
| `B.r` | `Metallic` | `GBuffer.Metallic` | `0..1` |
| `B.g` | `Specular` | `GBuffer.Specular` | Desktop 경로에서는 저장 전 8bit dither 적용 |
| `B.b` | `Roughness` | `GBuffer.Roughness` | `0..1` |
| `B.a` | `EncodeShadingModelIdAndSelectiveOutputMask(...)` | low 4bit = `ShadingModelID`, high 4bit = `SelectiveOutputMask` | `Default Lit`에서는 low 4bit 값이 `1` |

### `GBufferB.a` 비트 레이아웃

| 비트 | 의미 | 비고 |
| --- | --- | --- |
| bits `0..3` | `ShadingModelID` | `Default Lit = 1` |
| bit `4` | `HAS_ANISOTROPY_MASK` | 실제 탄젠트/anisotropy 데이터는 `GBufferF`에 있음 |
| bit `5` | `SKIP_PRECSHADOW_MASK` | set이면 `GBufferE`를 읽지 않고 prec shadow를 대체 처리 |
| bit `6` | `ZERO_PRECSHADOW_MASK` | `ALLOW_STATIC_LIGHTING`일 때만 prec shadow 대체값 0 의미 |
| bit `6` | `IS_FIRST_PERSON_MASK` | `!ALLOW_STATIC_LIGHTING`일 때 `ZERO_PRECSHADOW_MASK`와 비트 공유 |
| bit `7` | `SKIP_VELOCITY_MASK` | set이면 velocity buffer 무시 |

## GBufferC

| 채널 | 저장 값 | 디코드/해석 | 비고 |
| --- | --- | --- | --- |
| `C.r` | `EncodeBaseColor(BaseColor).r` | `DecodeBaseColor(InGBufferC.rgb).r` | 현재 구현은 값 그대로 저장, RT sRGB 정밀도 활용 |
| `C.g` | `EncodeBaseColor(BaseColor).g` | `DecodeBaseColor(InGBufferC.rgb).g` | 현재 구현은 값 그대로 저장 |
| `C.b` | `EncodeBaseColor(BaseColor).b` | `DecodeBaseColor(InGBufferC.rgb).b` | 현재 구현은 값 그대로 저장 |
| `C.a` | `GenericAO` 슬롯 | 설정에 따라 의미가 달라짐 | 아래 표 참고 |

### `GBufferC.a` 상세

| 조건 | 저장 값 | 디코드 후 의미 |
| --- | --- | --- |
| `GBUFFER_HAS_DIFFUSE_SAMPLE_OCCLUSION` | `DiffuseIndirectSampleOcclusion / 255` | `DiffuseIndirectSampleOcclusion` 복원, `GBufferAO`는 sample bit count로 재구성 |
| `!GBUFFER_HAS_DIFFUSE_SAMPLE_OCCLUSION && ALLOW_STATIC_LIGHTING` | `EncodeIndirectIrradiance(IndirectIrradiance * GBufferAO) + QuantizationBias / 255` | `IndirectIrradiance` 복원, `GBufferAO`는 `1`로 간주 |
| `!GBUFFER_HAS_DIFFUSE_SAMPLE_OCCLUSION && !ALLOW_STATIC_LIGHTING` | `GBufferAO` | `GBufferAO` 그대로 |

즉 `C.a`는 항상 순수 AO 채널이라고 보면 안 되고, 프로젝트 설정에 따라 `DiffuseIndirectSampleOcclusion` 또는 `IndirectIrradiance * AO` 인코딩값일 수 있다.

## GBufferD

| 채널 | 저장 값 | 디코드/해석 | 비고 |
| --- | --- | --- | --- |
| `D.r` | `0` | `0` | `Default Lit`은 custom GBuffer data를 쓰지 않음 |
| `D.g` | `0` | `0` | 동일 |
| `D.b` | `0` | `0` | 동일 |
| `D.a` | `0` | `0` | 동일 |

`GBufferD`는 다른 셰이딩 모델에서는 `CustomData.xyzw` 용도로 쓰지만, `Default Lit`은 `SetGBufferForShadingModel()`에서 `CustomData = 0`으로 유지된다.

## GBufferE

| 채널 | 저장 값 | 디코드/해석 | 비고 |
| --- | --- | --- | --- |
| `E.r` | `PrecomputedShadowFactors.r` | stationary/static shadow factor 채널 0 | Lightmass shadow channel |
| `E.g` | `PrecomputedShadowFactors.g` | stationary/static shadow factor 채널 1 | Lightmass shadow channel |
| `E.b` | `PrecomputedShadowFactors.b` | stationary/static shadow factor 채널 2 | Lightmass shadow channel |
| `E.a` | `PrecomputedShadowFactors.a` | stationary/static shadow factor 채널 3 | Lightmass shadow channel |

### `GBufferE` 주의사항

| 조건 | 디코드 결과 |
| --- | --- |
| `ALLOW_STATIC_LIGHTING && !SKIP_PRECSHADOW_MASK` | 저장된 `GBufferE` 값을 사용 |
| `ALLOW_STATIC_LIGHTING && SKIP_PRECSHADOW_MASK && ZERO_PRECSHADOW_MASK` | `0`으로 대체 |
| `ALLOW_STATIC_LIGHTING && SKIP_PRECSHADOW_MASK && !ZERO_PRECSHADOW_MASK` | `1`로 대체 |
| `!ALLOW_STATIC_LIGHTING` | 항상 `1`로 취급 |

## 참고 소스

- `Engine/Shaders/Private/DeferredShadingCommon.ush`
- `Engine/Shaders/Private/BasePassPixelShader.usf`
- `Engine/Shaders/Private/ShadingModelsMaterial.ush`
- `Engine/Shaders/Private/ShadingCommon.ush`
- `Engine/Shaders/Private/SceneData.ush`
