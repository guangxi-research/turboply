#pragma once

#include <cassert>
#include <span>
#include <functional>

namespace turboply {

    namespace detail {

        template <typename T>
        struct scalar_type_to_kind_struct;
        template <> struct scalar_type_to_kind_struct<int8_t>   { static constexpr ScalarKind value = ScalarKind::INT8; };
        template <> struct scalar_type_to_kind_struct<uint8_t>  { static constexpr ScalarKind value = ScalarKind::UINT8; };
        template <> struct scalar_type_to_kind_struct<int16_t>  { static constexpr ScalarKind value = ScalarKind::INT16; };
        template <> struct scalar_type_to_kind_struct<uint16_t> { static constexpr ScalarKind value = ScalarKind::UINT16; };
        template <> struct scalar_type_to_kind_struct<int32_t>  { static constexpr ScalarKind value = ScalarKind::INT32; };
        template <> struct scalar_type_to_kind_struct<uint32_t> { static constexpr ScalarKind value = ScalarKind::UINT32; };
        template <> struct scalar_type_to_kind_struct<float>    { static constexpr ScalarKind value = ScalarKind::FLOAT32; };
        template <> struct scalar_type_to_kind_struct<double>   { static constexpr ScalarKind value = ScalarKind::FLOAT64; };
        template <typename T> inline constexpr ScalarKind scalar_type_to_kind = scalar_type_to_kind_struct<std::decay_t<T>>::value;

        template<size_t N>
        struct fixed_string {
            char data[N];
            constexpr fixed_string(const char(&str)[N]) { std::copy_n(str, N, data); }

            auto operator<=>(const fixed_string&) const = default;
            constexpr operator const char* () const { return data; }
        };

        template <typename T>
        struct container_scalar_type_struct { using type = T;  };
        template <typename T, typename Alloc>
        struct container_scalar_type_struct<std::vector<T, Alloc>> { using type = T; };
        template <typename T, std::size_t N>
        struct container_scalar_type_struct<std::array<T, N>> { using type = T; };
        template <typename T>
        using container_scalar_type = typename container_scalar_type_struct<T>::type;
        
        template <fixed_string ElementName, typename RowT, fixed_string... PropertyNames>
            requires std::is_arithmetic_v<container_scalar_type<RowT>>
        struct PropertySpec {
            using RowType = RowT;
            using ScalarType = container_scalar_type<RowType>;
            using ColumnData = std::vector<RowType>;
            using ColumnView = std::span<RowType>;

            static constexpr auto scalar_kind = scalar_type_to_kind<ScalarType>;
            static constexpr std::string_view element_name{ ElementName };
            static constexpr size_t property_num = sizeof...(PropertyNames);
            static constexpr std::array<std::string_view, property_num> property_names = { std::string_view(PropertyNames)... };

            PropertySpec(ColumnData& column_data)
                : _column_view(column_data), _column_data(&column_data) {}

            PropertySpec(const ColumnData& column_data)
                : _column_view(const_cast<RowType*>(column_data.data()), column_data.size()), _column_data(nullptr) {}

            PropertySpec(ColumnView column_view)
                : _column_view(column_view), _column_data(nullptr) {}

            PropertySpec(std::span<const RowType> column_view)
                : _column_view(const_cast<RowType*>(column_view.data()), column_view.size()), _column_data(nullptr) {}

            ColumnView& operator()() { return _column_view; }
            const ColumnView& operator()() const { return _column_view; }

            void attach(const PlyElement& elem) {
                if (element_name == elem.name) {
                    if (_column_data) {
                        _column_data->resize(elem.count);
                        _column_view = std::span<RowType>(*_column_data);
                    }
                    else {
                        if (_column_view.size() != elem.count)
                            throw std::runtime_error("Ply element count mismatch with provided span");
                    }
                }
            }

            virtual PlyElement create() const {
                PlyElement elem;
                elem.name = std::string(element_name);
                elem.count = this->_column_view.size();
                elem.properties.resize(property_num);

                [&] <size_t... Is>(std::index_sequence<Is...>) {
                    ([&]() {
                        elem.properties[Is].name = std::string(property_names[Is]);
                        elem.properties[Is].valueKind = scalar_kind;
                        }(), ...);
                }(std::make_index_sequence<property_num>{});

                return elem;
            }

        private:
            ColumnView _column_view;
            ColumnData* _column_data;
        };

        template <typename T, typename TargetType>
        concept ByteCompatible = std::is_standard_layout_v<T> && std::is_trivial_v<T> && (sizeof(T) == sizeof(TargetType));
    }

template <detail::fixed_string ElementName, typename T, detail::fixed_string... PropertyNames>
struct ScalarSpec
    : detail::PropertySpec<ElementName, std::array<T, sizeof...(PropertyNames)>, PropertyNames...> {

    using RowType = std::array<T, sizeof...(PropertyNames)>;
    using Base = detail::PropertySpec<ElementName, RowType, PropertyNames...>;
    using Base::property_num;
    using Base::property_names;
    using Base::Base;

    ScalarSpec(std::vector<T>& column_data) requires (property_num == 1)
        : Base(reinterpret_cast<typename Base::ColumnData&>(column_data)) {}

    ScalarSpec(const std::vector<T>& column_data) requires (property_num == 1)
        : Base(reinterpret_cast<const typename Base::ColumnData&>(column_data)) {}

    ScalarSpec(std::span<T> column_view) requires (property_num == 1)
        : Base(typename Base::ColumnView(reinterpret_cast<RowType*>(column_view.data()), column_view.size())) {}

    ScalarSpec(std::span<const T> column_view) requires (property_num == 1)
        : Base(std::span<const RowType>(reinterpret_cast<const RowType*>(column_view.data()), column_view.size())) {}

    template <detail::ByteCompatible<RowType> UserT>
    ScalarSpec(std::vector<UserT>& column_data)
        : Base(reinterpret_cast<typename Base::ColumnData&>(column_data)) {}

    template <detail::ByteCompatible<RowType> UserT>
    ScalarSpec(const std::vector<UserT>& column_data)
        : Base(reinterpret_cast<const typename Base::ColumnData&>(column_data)) {}

    template <detail::ByteCompatible<RowType> UserT>
    ScalarSpec(std::span<UserT> column_view)
        : Base(typename Base::ColumnView(reinterpret_cast<RowType*>(column_view.data()), column_view.size())) {}

    template <detail::ByteCompatible<RowType> UserT>
    ScalarSpec(std::span<const UserT> column_view)
        : Base(std::span<const RowType>(reinterpret_cast<const RowType*>(column_view.data()), column_view.size())) {}
};

template <detail::fixed_string ElementName, typename T, detail::fixed_string PropertyName, typename ListType = uint8_t, std::size_t Length = 0>
struct ListSpec
    : detail::PropertySpec<
    ElementName,
    std::conditional_t<(Length == 0), std::vector<T>, std::array<T, Length>>,
    PropertyName>
{
    static constexpr auto list_kind = detail::scalar_type_to_kind<ListType>;
    static constexpr auto list_length = Length;

    using RowType = std::conditional_t<(Length == 0), std::vector<T>, std::array<T, Length>>;
    using Base = detail::PropertySpec<ElementName, RowType, PropertyName>;
    using Base::Base;

    virtual PlyElement create() const override {
        PlyElement elem = Base::create();
        elem.properties[0].listKind = list_kind;
        return elem;
    }
};

using VertexSpec = ScalarSpec<"vertex", float, "x", "y", "z">;
using NormalSpec = ScalarSpec<"vertex", float, "nx", "ny", "nz">;
using ColorSpec = ScalarSpec<"vertex", uint8_t, "red", "green", "blue">;
using FaceSpec = ListSpec<"face", uint32_t, "vertex_indices", uint8_t, 3>; // 定长三角面

//////////////////////////////////////////////////////////////////////////

    namespace detail {

        template <typename A, typename B>
        constexpr bool specs_conflict() {
            if (A::element_name != B::element_name) return false;

            for (const auto& p1 : A::property_names)
                for (const auto& p2 : B::property_names)
                    if (p1 == p2) return true;

            return false;
        }

        template <typename... Args>
        struct ConflictChecker;

        template <>
        struct ConflictChecker<> {
            static constexpr bool value = false;
        };

        template <typename Head, typename... Tail>
        struct ConflictChecker<Head, Tail...> {
            static constexpr bool value = (specs_conflict<Head, Tail>() || ...) || ConflictChecker<Tail...>::value;
        };

        template <typename... Specs>
        constexpr bool check_property_conflicts() {
            return ConflictChecker<std::decay_t<Specs>...>::value;
        }

    }

template <typename... Specs>
void bind_reader(
    PlyStreamReader& reader,
    Specs&... specs
) {
    static_assert(!detail::check_property_conflicts<Specs...>(), 
        "Multiple specs bind to the SAME property of the SAME element.");

    for (const auto& elem : reader.getElements()) {
        if (elem.count == 0) continue;

        using RowReader = std::function<void(size_t)>;
        std::vector<RowReader> rowReaders(elem.properties.size());

        for (size_t pi = 0; pi < elem.properties.size(); ++pi) {
            const auto& prop = elem.properties[pi];

            rowReaders[pi] = prop.listKind
                ? RowReader{ [&reader, &prop](size_t) {
                auto n = ply_cast<uint32_t>(reader.readScalar(*prop.listKind));
                for (uint32_t k = 0; k < n; ++k)
                    reader.readScalar(prop.valueKind);
                } }
                : RowReader{ [&reader, &prop](size_t) {
                    reader.readScalar(prop.valueKind);
                } };
        }

        ([&]() {
            using SpecT = std::decay_t<decltype(specs)>;

            if (SpecT::element_name == elem.name) {
                specs.attach(elem);

                auto property_indices = [&]<size_t... Is>(std::index_sequence<Is...>) {
                    auto propIdx = [&](std::string_view propName) {
                        auto it = std::find_if(elem.properties.begin(), elem.properties.end(),
                            [&](const auto& p) { return p.name == propName; });
                        if (it == elem.properties.end())
                            throw std::runtime_error(std::format("Missing property '{}' in '{}'", propName, elem.name));
                        return static_cast<size_t>(std::distance(elem.properties.begin(), it));
                        };
                    return std::array<std::size_t, SpecT::property_num>{propIdx(SpecT::property_names[Is])...
                    };
                }(std::make_index_sequence<SpecT::property_num>{});

                for (size_t spec_prop_idx = 0; spec_prop_idx < specs.property_num; ++spec_prop_idx) {
                    using ScalarType = typename SpecT::ScalarType;
                    size_t pi = property_indices[spec_prop_idx];
                    const auto& prop = elem.properties[pi];

                    rowReaders[pi] = [&reader, &specs, spec_prop_idx, &prop](size_t row) {
                        auto& row_data = specs()[row];
                        if constexpr (requires { specs.list_kind; }) {
                            auto n = ply_cast<uint32_t>(reader.readScalar(*prop.listKind));

                            if constexpr (requires(decltype(row_data) c, size_t s) { c.resize(s); }) {
                                row_data.resize(n);
                            }

                            for (uint32_t k = 0; k < n; ++k) {
                                auto v = reader.readScalar(prop.valueKind);
                                if (k < row_data.size())
                                    row_data[k] = ply_cast<ScalarType>(v);
                            }
                        }
                        else {
                            auto v = reader.readScalar(prop.valueKind);
                            row_data[spec_prop_idx] = ply_cast<ScalarType>(v);
                        }
                        };
                }
            }
            }(), ...);

        for (size_t ri = 0; ri < elem.count; ++ri) {
            for (const auto& rr : rowReaders) rr(ri);
        }
    }
}

template <typename... Specs>
void bind_writer(
    PlyStreamWriter& writer,
    const Specs&... specs
) {
    static_assert(!detail::check_property_conflicts<Specs...>(), 
        "Multiple specs bind to the SAME property of the SAME element.");

    std::vector<PlyElement> elements;
    elements.reserve(sizeof...(specs));

    ([&] {
        auto elem = specs.create();

        auto it = std::find_if(elements.begin(), elements.end(),
            [&](const auto& e) { return e.name == elem.name; });

        if (it != elements.end()) {
            if (it->count != elem.count)
                throw std::runtime_error(std::format("Element count mismatch for '{}'", elem.name));

            it->properties.insert(it->properties.end(),
                std::make_move_iterator(elem.properties.begin()),
                std::make_move_iterator(elem.properties.end())); // merge same name element
        }
        else
            elements.push_back(std::move(elem));
        }(), ...);

    for (auto& elem : elements)
        writer.addElement(elem);

    writer.writeHeader();

    for (const auto& elem : elements) {
        for (size_t ri = 0; ri < elem.count; ++ri) {
            ([&] {
                if (specs.element_name == elem.name) {
                    const auto& row_data = specs()[ri];

                    if constexpr (requires { specs.list_kind; }) { // List Property
                        writer.writeScalar(static_cast<uint32_t>(row_data.size()), specs.list_kind); // List Count
                        for (const auto& v : row_data) {// List Content
                            writer.writeScalar(v);
                        }
                    }
                    else {// Scalar Property
                        for (size_t pi = 0; pi < specs.property_num; ++pi) {
                            writer.writeScalar(row_data[pi]);
                        }
                    }
                }
                }(), ...);

            writer.writeLineEnd();
        }
    }

    writer.flush();
}

}
