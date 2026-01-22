# TurboPLY [![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](./LICENSE)
<img width="300" height="300" alt="turboply" src="https://github.com/user-attachments/assets/a0986559-5e4c-49bf-91e4-f44e8ef327df" /><img width="400" height="300" alt="image" src="https://github.com/user-attachments/assets/4b78aa0a-c02a-47ca-8a83-97f8336184e6" />


TurboPLY is a lightweight, high-performance C++ library for reading and writing PLY (Polygon File Format) files. It is designed for modern geometry pipelines where throughput, low memory overhead, and extensibility are critical. The library supports binary little-endian and ASCII formats, and provides optional memory-mapped file I/O for zero-copy, high-throughput access to large datasets. Big-endian format is intentionally not supported to keep the implementation simple and fast. Beyond performance, TurboPLY is engineered for extreme ease of use. It features an intuitive, declarative binding API that allows developers to map complex PLY properties to C++ containers with minimal code.

## Extreme Ease of Use

TurboPLY is engineered for developer efficiency. Unlike traditional PLY libraries that require manual attribute-by-attribute parsing, TurboPLY uses a declarative binding API, handling memory mapping and serialization automatically. This high-throughput design is particularly beneficial for Gaussian Splatting, which involves dozens of Spherical Harmonic (SH) coefficients per splat.

---

## Features

- 🚀 High-performance PLY reader and writer
- 📄 Supports ASCII and binary little-endian formats
- 🧠 Optional memory-mapped file I/O (zero-copy loading)
- 🧩 Compile-time property binding via templates
- 🧱 Structured access to vertices, faces, normals, and custom attributes
- ⚙️ Suitable for large-scale point clouds and meshes

---

## Requirements

- C++20 or later (for non-type template parameters with string literals)
- A compiler supporting modern C++ (GCC 11+, Clang 13+, MSVC 2022+)

---

## Basic Usage

### Reading a PLY file

```cpp
#include "turboply.hpp"

void load_ply(
	const std::string& filename,
	std::vector<std::array<float, 3>>& vertices,
	std::vector<std::array<float, 3>>& normals,
	std::vector<float>& weights,
	std::vector<float>& accuracies,
	std::vector<float>& samplings,
	std::vector<uint8_t>& types,
	std::vector<std::vector<uint32_t>>& visibilities,
	std::vector<std::array<uint32_t, 3>>& facets
) {
	using namespace turboply;

	PlyFileReader reader(filename, true);

	reader.parseHeader();

	VertexSpec v_spec{ vertices };
	FaceSpec   f_spec{ facets };

	NormalSpec n_spec{ normals };

	using WeightSpec = ScalarSpec<"vertex", float, "weight">;
	WeightSpec w_spec{ weights };

	using AccuracySpec = ScalarSpec<"vertex", float, "accuracy">;
	AccuracySpec a_spec{ accuracies };

	using SamplingSpec = ScalarSpec<"vertex", float, "sampling">;
	SamplingSpec s_spec{ samplings };

	using TypeSpec = ScalarSpec<"vertex", uint8_t, "type">;
	TypeSpec t_spec{ types };

	using VisibilitySpec = ListSpec<"vertex", uint32_t, "visibility">;
	VisibilitySpec visib_spec{ visibilities };

	// Bind the reader with the following capabilities:
    // 1. Order-Independent: The order of Specs passed does not need to match the file's internal structure.
    // 2. Selective/Optional Reading: Only the bound attributes are processed. You can easily omit any Spec 
    //    (e.g., by commenting it out) to skip loading unnecessary data from the file.
    bind_reader(reader, f_spec, n_spec, v_spec, w_spec, visib_spec, /*a_spec, s_spec, */t_spec);
}
```

## Writing a PLY file

```cpp
#include "turboply.hpp"

void save_ply(
	const std::string& filename,
	const std::vector<std::array<float, 3>>& vertices,
	const std::vector<std::array<float, 3>>& normals,
	const std::vector<float>& weights,
	const std::vector<float>& accuracies,
	const std::vector<float>& samplings,
	const std::vector<uint8_t>& types,
	const std::vector<std::vector<uint32_t>>& visibilities,
	const std::vector<std::array<uint32_t, 3>>& facets,
	bool binary
) {
	using namespace turboply;

	PlyFileWriter writer(filename, binary ? PlyFormat::BINARY : PlyFormat::ASCII, true, 50 * 1024 * 1024);

	VertexSpec v_spec{ vertices };
	NormalSpec n_spec{ normals };

	using WeightSpec = ScalarSpec<"vertex", float, "weight">;
	WeightSpec w_spec{ weights };

	using AccuracySpec = ScalarSpec<"vertex", float, "accuracy">;
	AccuracySpec a_spec{ accuracies };

	using SamplingSpec = ScalarSpec<"vertex", float, "sampling">;
	SamplingSpec s_spec{ samplings };

	using TypeSpec = ScalarSpec<"vertex", uint8_t, "type">;
	TypeSpec t_spec{ types };

	using VisibilitySpec = ListSpec<"vertex", uint32_t, "visibility">;
	VisibilitySpec visib_spec{ visibilities };

	std::vector<std::array<uint32_t, 3>>& facets_ = const_cast<std::vector<std::array<uint32_t, 3>>&>(facets);
	FaceSpec   f_spec{ facets_ };

	// Bind the writer with strict ordering:
    // 1. Elements and properties are serialized in the EXACT sequence they are passed here.
    // 2. Ensure the order follows your desired PLY header structure.
    bind_writer(writer, v_spec, n_spec, w_spec, a_spec, s_spec, t_spec, visib_spec, f_spec);
}
```
## Gaussian Splatting Example (3DGS) with LibTorch

TurboPLY is optimized for modern Deep Learning pipelines. The following example demonstrates how to load/save standard 3DGS attributes directly into **`torch::Tensor`**. 
By casting the Tensor's raw memory to `std::array` types, we maintain TurboPLY's high-performance declarative binding without unnecessary data duplication.

```cpp
// 以下示例代码是骂了AI Gemini3Pro N次以后写出来的，最后这版看了一眼应该没有大问题，没有实际测试
#include <torch/torch.h>
#include "turboply.hpp"

static constexpr int SH_DC_DIM = 3;
static constexpr int SH_REST_DIM = 45;

using PositionSpec = ScalarSpec<"vertex", float, "x", "y", "z">;
using ScaleSpec    = ScalarSpec<"vertex", float, "scale_0", "scale_1", "scale_2">;
using RotationSpec = ScalarSpec<"vertex", float, "rot_0", "rot_1", "rot_2", "rot_3">;
using OpacitySpec  = ScalarSpec<"vertex", float, "opacity">;
using SHDCSpec     = ScalarSpec<"vertex", float, "f_dc_0", "f_dc_1", "f_dc_2">;
using SHRestSpec   = ScalarSpec<"vertex", float,
    "f_rest_0","f_rest_1","f_rest_2","f_rest_3","f_rest_4",
    "f_rest_5","f_rest_6","f_rest_7","f_rest_8","f_rest_9",
    "f_rest_10","f_rest_11","f_rest_12","f_rest_13","f_rest_14",
    "f_rest_15","f_rest_16","f_rest_17","f_rest_18","f_rest_19",
    "f_rest_20","f_rest_21","f_rest_22","f_rest_23","f_rest_24",
    "f_rest_25","f_rest_26","f_rest_27","f_rest_28","f_rest_29",
    "f_rest_30","f_rest_31","f_rest_32","f_rest_33","f_rest_34",
    "f_rest_35","f_rest_36","f_rest_37","f_rest_38","f_rest_39",
    "f_rest_40","f_rest_41","f_rest_42","f_rest_43","f_rest_44"
>;

void load_gaussian_splat_ply(
    const std::string& filename,
    torch::Tensor& positions,
    torch::Tensor& scales,
    torch::Tensor& rotations,
    torch::Tensor& opacities,
    torch::Tensor& sh_dc,
    torch::Tensor& sh_rest,
    torch::Device device
) {
    PlyFileReader reader(filename, true);
    reader.parseHeader();

    size_t count = 0;
    for (const auto& el : reader.getElements())
        if (el.name == "vertex") { count = el.count; break; }

    // Allocate CPU vectors for TurboPLY
    std::vector<std::array<float,3>> vertices(count);
    std::vector<std::array<float,3>> scales_vec(count);
    std::vector<std::array<float,4>> rotations_vec(count);
    std::vector<float>              opacities_vec(count);
    std::vector<std::array<float,SH_DC_DIM>> sh_dc_vec(count);
    std::vector<std::array<float,SH_REST_DIM>> sh_rest_vec(count);

    // Bind and read
    bind_reader(reader,
        PositionSpec{ vertices },
        ScaleSpec{ scales_vec },
        RotationSpec{ rotations_vec },
        OpacitySpec{ opacities_vec },
        SHDCSpec{ sh_dc_vec },
        SHRestSpec{ sh_rest_vec }
    );

    auto options = torch::TensorOptions().dtype(torch::kFloat32);

    // Wrap CPU vectors into tensors and move to device
    positions = torch::from_blob(vertices.data(), {static_cast<long>(count),3}, options).to(device, false, true);
    scales    = torch::from_blob(scales_vec.data(), {static_cast<long>(count),3}, options).to(device, false, true);
    rotations = torch::from_blob(rotations_vec.data(), {static_cast<long>(count),4}, options).to(device, false, true);
    opacities = torch::from_blob(opacities_vec.data(), {static_cast<long>(count),1}, options).to(device, false, true);
    sh_dc     = torch::from_blob(sh_dc_vec.data(), {static_cast<long>(count),SH_DC_DIM}, options).to(device, false, true);
    sh_rest   = torch::from_blob(sh_rest_vec.data(), {static_cast<long>(count),SH_REST_DIM}, options).to(device, false, true);
}

// ---------- Save GPU Tensor to PLY using from_blob + copy_ ----------
void save_gaussian_splat_ply(
    const std::string& filename,
    const torch::Tensor& positions_gpu,
    const torch::Tensor& scales_gpu,
    const torch::Tensor& rotations_gpu,
    const torch::Tensor& opacities_gpu,
    const torch::Tensor& sh_dc_gpu,
    const torch::Tensor& sh_rest_gpu,
    bool binary
) {
    size_t nPoints = positions_gpu.size(0);

    // Allocate CPU vectors
    std::vector<std::array<float,3>> out_vertices(nPoints);
    std::vector<std::array<float,3>> out_scales(nPoints);
    std::vector<std::array<float,4>> out_rotations(nPoints);
    std::vector<float>              out_opacities(nPoints);
    std::vector<std::array<float,SH_DC_DIM>> out_sh_dc(nPoints);
    std::vector<std::array<float,SH_REST_DIM>> out_sh_rest(nPoints);

    // Copy GPU tensor → CPU vector using from_blob + copy_
    torch::from_blob(out_vertices.data(), {static_cast<long>(nPoints),3}, torch::kFloat32)
        .copy_(positions_gpu.contiguous().to(torch::kCPU));
    torch::from_blob(out_scales.data(), {static_cast<long>(nPoints),3}, torch::kFloat32)
        .copy_(scales_gpu.contiguous().to(torch::kCPU));
    torch::from_blob(out_rotations.data(), {static_cast<long>(nPoints),4}, torch::kFloat32)
        .copy_(rotations_gpu.contiguous().to(torch::kCPU));
    torch::from_blob(out_opacities.data(), {static_cast<long>(nPoints),1}, torch::kFloat32)
        .copy_(opacities_gpu.contiguous().to(torch::kCPU));
    torch::from_blob(out_sh_dc.data(), {static_cast<long>(nPoints), SH_DC_DIM}, torch::kFloat32)
        .copy_(sh_dc_gpu.contiguous().to(torch::kCPU));
    torch::from_blob(out_sh_rest.data(), {static_cast<long>(nPoints), SH_REST_DIM}, torch::kFloat32)
        .copy_(sh_rest_gpu.contiguous().to(torch::kCPU));

    // Write PLY
    PlyFileWriter writer(filename, binary ? PlyFormat::BINARY : PlyFormat::ASCII, true, 500 * 1024 * 1024);
    bind_writer(writer,
        PositionSpec{ out_vertices },
        ScaleSpec{ out_scales },
        RotationSpec{ out_rotations },
        OpacitySpec{ out_opacities },
        SHDCSpec{ out_sh_dc },
        SHRestSpec{ out_sh_rest }
    );
}

```

## Custom Type Binding and Preallocation Example

```cpp

struct Point {
    float x, y, z;
};

PlyFileReader reader("SampleData\\ascii.ply", true);
reader.parseHeader();

auto elem = reader.getElements()[0];
assert(elem.name == "vertex");

auto pts = new Point[elem.count];

std::vector<Point> normals;
normals.resize(elem.count);

std::vector<float> weight;
using WeightSpec = ScalarSpec<"vertex", float, "weight">;

VertexSpec v_spec{ std::span<Point>(pts, elem.count) }; // 预分配
NormalSpec n_spec{ std::span(normals) }; // 预分配
WeightSpec w_spec{ weight }; // 绑定后分配

bind_reader(reader, n_spec, v_spec, w_spec);

```

## Memory-Mapped I/O

TurboPLY optionally uses memory-mapped files to avoid unnecessary data copies when loading or writing large PLY files.

Benefits:

- Zero-copy parsing
- Reduced memory usage
- Faster startup for large datasets
- OS-level file caching

Enable it by passing `true` to the reader or writer constructor.

---

## Performance Notes

- Optimized for sequential access patterns
- Minimal dynamic allocation
- Optional large-buffer preallocation for writers
- No big-endian support to reduce branching and parsing complexity

---

## License

MIT License  
Copyright (c) 2026 TAO

---

## Contributing

Contributions are welcome:

- Bug fixes
- Performance improvements
- Additional PLY features
- Platform compatibility patches
- Documentation improvements

---

## Contact

Maintainer: TAO  
Email: 12804985@qq.com


