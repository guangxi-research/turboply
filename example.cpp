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

	bind_reader(reader, f_spec, n_spec, v_spec, w_spec, visib_spec, a_spec, s_spec, t_spec);
}

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

	bind_writer(writer, v_spec, n_spec, w_spec, a_spec, s_spec, t_spec, visib_spec, f_spec);// 必须保证顺序
}

//////////////////////////////////////////////////////////////////////////

namespace turboply::ext {

    static constexpr int SH_DC_DIM = 3;
    static constexpr int SH_REST_DIM = 45;

    using PositionSpec = UniformSpec<"vertex", float, "x", "y", "z">;
    using ScaleSpec = UniformSpec<"vertex", float, "scale_0", "scale_1", "scale_2">;
    using RotationSpec = UniformSpec<"vertex", float, "rot_0", "rot_1", "rot_2", "rot_3">;
    using OpacitySpec = ScalarSpec<"vertex", float, "opacity">;
    using SHDCSpec = UniformSpec<"vertex", float, "f_dc_0", "f_dc_1", "f_dc_2">;
    using SHRestSpec = UniformSpec<"vertex", float,
        "f_rest_0", "f_rest_1", "f_rest_2", "f_rest_3", "f_rest_4",
        "f_rest_5", "f_rest_6", "f_rest_7", "f_rest_8", "f_rest_9",
        "f_rest_10", "f_rest_11", "f_rest_12", "f_rest_13", "f_rest_14",
        "f_rest_15", "f_rest_16", "f_rest_17", "f_rest_18", "f_rest_19",
        "f_rest_20", "f_rest_21", "f_rest_22", "f_rest_23", "f_rest_24",
        "f_rest_25", "f_rest_26", "f_rest_27", "f_rest_28", "f_rest_29",
        "f_rest_30", "f_rest_31", "f_rest_32", "f_rest_33", "f_rest_34",
        "f_rest_35", "f_rest_36", "f_rest_37", "f_rest_38", "f_rest_39",
        "f_rest_40", "f_rest_41", "f_rest_42", "f_rest_43", "f_rest_44"
    >;

    void load_gaussian_splat_ply(
        const std::string& filename,
        std::vector<std::array<float, 3>>& positions,
        std::vector<std::array<float, 3>>& scales,
        std::vector<std::array<float, 4>>& rotations,
        std::vector<float>& opacities,
        std::vector<std::array<float, SH_DC_DIM>>& sh_dc,
        std::vector<std::array<float, SH_REST_DIM>>& sh_rest
    ) {
        PlyFileReader reader(filename, true);
        reader.parseHeader();

        PositionSpec pos_spec{ positions };
        ScaleSpec scale_spec{ scales };
        RotationSpec rot_spec{ rotations };
        OpacitySpec op_spec{ opacities };
        SHDCSpec shdc_spec{ sh_dc };
        SHRestSpec shrest_spec{ sh_rest };

        bind_reader(reader
            , pos_spec
            , scale_spec
            , rot_spec
            , op_spec
            , shdc_spec
            //, shrest_spec
        );
    }

    void save_gaussian_splat_ply(
        const std::string& filename,
        const std::vector<std::array<float, 3>>& positions,
        const std::vector<std::array<float, 3>>& scales,
        const std::vector<std::array<float, 4>>& rotations,
        const std::vector<float>& opacities,
        const std::vector<std::array<float, SH_DC_DIM>>& sh_dc,
        const std::vector<std::array<float, SH_REST_DIM>>& sh_rest,
        bool binary,
        size_t reserve_size
    ) {
        PlyFileWriter writer(
            filename,
            binary ? PlyFormat::BINARY : PlyFormat::ASCII,
            true,
            reserve_size
        );

        PositionSpec pos_spec{ positions };
        ScaleSpec scale_spec{ scales };
        RotationSpec rot_spec{ rotations };
        OpacitySpec op_spec{ opacities };
        SHDCSpec shdc_spec{ sh_dc };
        SHRestSpec shrest_spec{ sh_rest };

        bind_writer(writer
            , pos_spec
            , scale_spec
            , rot_spec
            , op_spec
            , shdc_spec
            //, shrest_spec
        );
    }

}
