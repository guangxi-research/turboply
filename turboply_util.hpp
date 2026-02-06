#pragma once

#include <span>
#include <functional>

namespace turboply {

    template<typename... Types>
        requires ((std::is_arithmetic_v<Types> ||
            requires { typename Types::value_type;
                requires std::is_arithmetic_v<typename Types::value_type>; }
            ) && ...)
    struct RecordTuple {
        private:
            template<typename... Ts>
            struct Storage;

            template<typename T>
            struct Storage<T> {
                T first;
                auto operator<=>(const Storage&) const = default;
            };

            template<typename Head, typename Next, typename... Tail>
            struct Storage<Head, Next, Tail...> {
                Head first;
                Storage<Next, Tail...> rest;
                auto operator<=>(const Storage&) const = default;
            };

            template<std::size_t I, typename S>
            static constexpr auto& get_impl(S& s) {
                if constexpr (I == 0)
                    return s.first;
                else
                    return get_impl<I - 1>(s.rest);
            }

            template<std::size_t I, typename S>
            static constexpr const auto& get_impl(const S& s) {
                if constexpr (I == 0)
                    return s.first;
                else
                    return get_impl<I - 1>(s.rest);
            }

            template<std::size_t I, typename... Ts>
            struct type_at;

            template<typename Head, typename... Tail>
            struct type_at<0, Head, Tail...> { using type = Head; };

            template<std::size_t I, typename Head, typename... Tail>
            struct type_at<I, Head, Tail...> { using type = typename type_at<I - 1, Tail...>::type; };

            Storage<Types...> storage;

        public:
            constexpr RecordTuple() = default;

            template<typename... Args>
            constexpr explicit RecordTuple(Args&&... args)
                : storage{ std::forward<Args>(args)... } {
            }

            auto operator<=>(const RecordTuple&) const = default;

            static constexpr std::size_t size() {
                return sizeof...(Types);
            }

            template<std::size_t I>
            using field_type = typename type_at<I, Types...>::type;

            template<std::size_t I>
            constexpr auto& get()& {
                static_assert(I < sizeof...(Types), "Index out of bounds");
                return get_impl<I>(storage);
            }

            template<std::size_t I>
            constexpr const auto& get() const& {
                static_assert(I < sizeof...(Types), "Index out of bounds");
                return get_impl<I>(storage);
            }

            template<std::size_t I>
            constexpr auto&& get()&& {
                static_assert(I < sizeof...(Types), "Index out of bounds");
                return std::move(get_impl<I>(storage));
            }

            template<std::size_t N>
            friend constexpr auto& get(RecordTuple& t) {
                return t.template get<N>();
            }

            template<std::size_t N>
            friend constexpr const auto& get(const RecordTuple& t) {
                return t.template get<N>();
            }

            template<std::size_t N>
            friend constexpr auto&& get(RecordTuple&& t) {
                return std::move(t).template get<N>();
            }
    };

    template<>
    struct RecordTuple<> {
        constexpr RecordTuple() = default;
        auto operator<=>(const RecordTuple<>&) const = default;
    };

    template<typename... Types>
    struct std::tuple_size<turboply::RecordTuple<Types...>>
        : std::integral_constant<std::size_t, sizeof...(Types)> {
    };

    template<std::size_t I, typename... Types>
    struct std::tuple_element<I, turboply::RecordTuple<Types...>> {
    private:
        template<std::size_t N, typename Head, typename... Tail>
        struct TypeAt { using type = typename TypeAt<N - 1, Tail...>::type; };

        template<typename Head, typename... Tail>
        struct TypeAt<0, Head, Tail...> { using type = Head; };

    public:
        using type = typename TypeAt<I, Types...>::type;
    };

    namespace detail {

        template<size_t N>
        struct fixed_string {
            char data[N];
            constexpr fixed_string(const char(&str)[N]) { std::copy_n(str, N, data); }

            auto operator<=>(const fixed_string&) const = default;
            constexpr operator const char* () const { return data; }
        };

        template <detail::fixed_string ElementName, typename RowT, detail::fixed_string... PropertyNames>
        struct PropertySpec {
        private:
            static_assert(RowT::size() == sizeof...(PropertyNames),
                "PropertySpec: Property names count must match RecordTuple field count.");

            template <typename T>
            struct column_type_traits {
                static constexpr ScalarKind get_scalar_kind() {
                    using U = std::decay_t<T>;
                    if constexpr (std::is_same_v<U, float>)    return ScalarKind::FLOAT32;
                    if constexpr (std::is_same_v<U, double>)   return ScalarKind::FLOAT64;
                    if constexpr (std::is_same_v<U, int32_t>)  return ScalarKind::INT32;
                    if constexpr (std::is_same_v<U, uint32_t>) return ScalarKind::UINT32;
                    if constexpr (std::is_same_v<U, int16_t>)  return ScalarKind::INT16;
                    if constexpr (std::is_same_v<U, uint16_t>) return ScalarKind::UINT16;
                    if constexpr (std::is_same_v<U, uint8_t>)  return ScalarKind::UINT8;
                    if constexpr (std::is_same_v<U, int8_t>)   return ScalarKind::INT8;
                    if constexpr (std::is_same_v<U, char>)     return ScalarKind::INT8;
                    return ScalarKind::UNUSED;
                }

                using ScalarType = T;
                static constexpr ScalarKind value_kind = get_scalar_kind();
                static constexpr ScalarKind list_kind = ScalarKind::UNUSED;
            };

            template <typename T, typename Alloc>
            struct column_type_traits<std::vector<T, Alloc>> {
                using ScalarType = T;
                static constexpr ScalarKind value_kind = column_type_traits<T>::get_scalar_kind();
                static constexpr ScalarKind list_kind = ScalarKind::UINT32;
            };

            template <typename T, size_t N>
            struct column_type_traits<std::array<T, N>> {
                using ScalarType = T;
                static constexpr ScalarKind value_kind = column_type_traits<T>::get_scalar_kind();
                static constexpr ScalarKind list_kind = ScalarKind::UINT8;
            };

        public:
            using RowType = RowT;
            using ColumnData = std::vector<RowType>;
            using ColumnView = std::span<RowType>;

            static constexpr std::string_view element_name{ ElementName };
            static constexpr size_t property_num = sizeof...(PropertyNames);

            PropertySpec(ColumnData& column_data)
                : _column_view{ column_data }, _column_data{ &column_data } {
            }

            PropertySpec(const ColumnData& column_data)
                : _column_view{ const_cast<RowType*>(column_data.data()), column_data.size() }, _column_data{ nullptr } {
            }

            PropertySpec(ColumnView column_view)
                : _column_view{ column_view }, _column_data{ nullptr } {
            }

            PropertySpec(std::span<const RowType> column_view)
                : _column_view{ const_cast<RowType*>(column_view.data()), column_view.size() }, _column_data{ nullptr } {
            }

            template <typename UserT>
            PropertySpec(std::vector<UserT>& column_data) requires (sizeof(UserT) == sizeof(RowType))
                : PropertySpec{ reinterpret_cast<ColumnData&>(column_data) } {
            }

            template <typename UserT>
            PropertySpec(const std::vector<UserT>& column_data) requires (sizeof(UserT) == sizeof(RowType))
                : PropertySpec{ reinterpret_cast<const ColumnData&>(column_data) } {
            }

            template <typename UserT>
            PropertySpec(std::span<UserT> column_view) requires (sizeof(UserT) == sizeof(RowType))
                : PropertySpec{ ColumnView(reinterpret_cast<RowType*>(column_view.data()), column_view.size()) } {
            }

            template <typename UserT>
            PropertySpec(std::span<const UserT> column_view) requires (sizeof(UserT) == sizeof(RowType))
                : PropertySpec{ std::span<const RowType>(reinterpret_cast<const RowType*>(column_view.data()), column_view.size()) } {
            }

            ColumnView& operator()() { return _column_view; }
            const ColumnView& operator()() const { return _column_view; }

            void resize(size_t n) {
                if (_column_data) {
                    _column_data->resize(n);
                    _column_view = std::span<RowType>(*_column_data);
                }
                else if (_column_view.size() != n) {
                    throw std::runtime_error(std::format(
                        "Ply Error: Element count mismatch. Element '{}' expects {} rows, but provided storage has {} rows."
                        , element_name, n, _column_view.size()));
                }
            }

            template <size_t I>
            struct ColumnInfo {
                using FieldType = typename RowType::template field_type<I>;
                using Traits = column_type_traits<FieldType>;
                using ScalarType = typename Traits::ScalarType;

                static constexpr auto __name = std::get<I>(std::tuple{ PropertyNames... });
                static constexpr std::string_view property_name{ __name };
                static constexpr ScalarKind value_kind = Traits::value_kind;
                static constexpr ScalarKind list_kind = Traits::list_kind;
            };

            PlyElement create() const {
                PlyElement elem;
                elem.name = std::string(element_name);
                elem.count = _column_view.size();

                [&] <size_t... Is>(std::index_sequence<Is...>) {
                    (elem.properties.push_back(
                        PlyElement::Property{
                            .name = std::string(ColumnInfo<Is>::property_name),
                            .valueKind = ColumnInfo<Is>::value_kind,
                            .listKind = ColumnInfo<Is>::list_kind
                        }
                    ), ...);
                }(std::make_index_sequence<property_num>{});

                return elem;
            }

        private:
            ColumnView _column_view;
            ColumnData* _column_data;
        };

        template <typename T>
        concept IsPropertySpec = requires(T & t) {
            [] <detail::fixed_string E, typename R, detail::fixed_string... Ps>
                (const PropertySpec<E, R, Ps...>&) {
            }(t);
        };

        template <typename T, size_t N>
        using repeat_type_t = typename decltype(
            []<size_t... Is>(std::index_sequence<Is...>) {
            return std::type_identity<RecordTuple<
                std::remove_cvref_t<decltype(((void)Is, std::declval<T>()))>...>>{};
        }(std::make_index_sequence<N>{}))::type;

    }

template <detail::fixed_string ElementName, typename T, detail::fixed_string... PropertyNames>
    requires std::is_arithmetic_v<T>
using UniformSpec = detail::PropertySpec<ElementName, detail::repeat_type_t<T, sizeof...(PropertyNames)>, PropertyNames...>;

template <detail::fixed_string ElementName, typename T, detail::fixed_string PropertyName>
using ScalarSpec = UniformSpec<ElementName, T, PropertyName>;

template <detail::fixed_string ElementName, typename T, detail::fixed_string PropertyName, size_t Length = 0>
    requires std::is_arithmetic_v<T>
using ListSpec = detail::PropertySpec<ElementName, RecordTuple<std::conditional_t<Length == 0, std::vector<T>, std::array<T, Length>>>, PropertyName>;

template <detail::fixed_string Name, typename T, detail::fixed_string... Props>
using CustomSpec = detail::PropertySpec<Name, T, Props...>;

using VertexSpec = UniformSpec<"vertex", float, "x", "y", "z">;
using NormalSpec = UniformSpec<"vertex", float, "nx", "ny", "nz">;
using ColorSpec = UniformSpec<"vertex", float, "red", "green", "blue">;
using FaceSpec = ListSpec<"face", uint32_t, "vertex_indices", 3>;

//////////////////////////////////////////////////////////////////////////

    namespace detail {

        template <typename A, typename B>
        constexpr bool prop_specs_conflict() {
            if constexpr (A::element_name != B::element_name) {
                return false;
            }
            else {
                constexpr auto check = []<size_t I, size_t J>() {
                    using PropA = typename A::template ColumnInfo<I>;
                    using PropB = typename B::template ColumnInfo<J>;
                    return PropA::property_name == PropB::property_name;
                };

                return[]<size_t... Is>(std::index_sequence<Is...>) {
                    return (([]<size_t I>() {
                        return[]<size_t... Js>(std::index_sequence<Js...>) {
                            return (check.template operator() < I, Js > () || ...);
                        }(std::make_index_sequence<B::property_num>{});
                    }.template operator() < Is > ()) || ...);
                }(std::make_index_sequence<A::property_num>{});
            }
        }

        template <typename... Args>
        struct ConflictChecker;

        template <>
        struct ConflictChecker<> {
            static constexpr bool value = false;
        };

        template <typename Head, typename... Tail>
        struct ConflictChecker<Head, Tail...> {
            static constexpr bool value = (prop_specs_conflict<Head, Tail>() || ...) || ConflictChecker<Tail...>::value;
        };

        template <typename... Specs>
        constexpr bool check_property_conflicts() {
            return ConflictChecker<std::decay_t<Specs>...>::value;
        }

    }

template <typename... Specs>
    requires (detail::IsPropertySpec<Specs> && ...)
void bind_reader(PlyStreamReader& reader, Specs&... specs) {
    static_assert(!detail::check_property_conflicts<Specs...>(),
        "Multiple specs bind to the SAME property of the SAME element.");

    reader.parseHeader();

    for (const auto& elem : reader.getElements()) {
        if (elem.count == 0) continue;

        using ColumnReader = std::function<void(size_t)>;
        std::vector<ColumnReader> columnReaders(elem.properties.size());

        for (size_t pi = 0; pi < elem.properties.size(); ++pi) {
            auto& prop = elem.properties[pi];
            columnReaders[pi] = [&reader, prop](size_t) {
                if (prop.listKind != ScalarKind::UNUSED) {
                    auto n = ply_cast<uint32_t>(reader.readScalar(prop.listKind));
                    for (uint32_t k = 0; k < n; ++k) 
                        reader.readScalar(prop.valueKind);
                }
                else {
                    reader.readScalar(prop.valueKind);
                }
            };
        }

        ([&](auto& spec) {
            using SpecT = std::decay_t<decltype(spec)>;

            if (SpecT::element_name != elem.name) return;
            spec.resize(elem.count);

            [&] <size_t... Is>(std::index_sequence<Is...>) {
                ([&]() {
                    using PI = typename SpecT::template ColumnInfo<Is>;

                    auto it = std::find_if(elem.properties.begin(), elem.properties.end(),
                        [](const auto& prop) { return prop.name == PI::property_name; });

                    if (it != elem.properties.end()) {
                        size_t pi = std::distance(elem.properties.begin(), it);
                        const auto& prop = *it; // runtime

                        columnReaders[pi] = [&reader, &spec, prop](size_t row_index) {
                            auto& row_item = get<Is>(spec()[row_index]);

                            if constexpr (PI::list_kind != ScalarKind::UNUSED) {
                                if (prop.listKind == ScalarKind::UNUSED)
                                    throw std::runtime_error(std::format(
                                        "Ply Read Error: Property '{}' type mismatch. Expected LIST, but found SCALAR in file."
                                        , PI::property_name));
                                
                                auto n = ply_cast<size_t>(reader.readScalar(prop.listKind));

                                [&](auto& container) {
                                    if constexpr (requires { container.resize(n); })
                                        container.resize(n);

                                    size_t capacity = 0;
                                    if constexpr (requires { container.size(); }) 
                                        capacity = container.size();

                                    size_t limit = std::min(n, capacity); 
                                    for (size_t k = 0; k < limit; ++k)
                                        container[k] = ply_cast<typename PI::ScalarType>(reader.readScalar(prop.valueKind));
                                    for (size_t k = limit; k < n; ++k)
                                        reader.readScalar(prop.valueKind); // 丢弃
                                }(row_item);
                            }
                            else {
                                if (prop.listKind != ScalarKind::UNUSED)
                                    throw std::runtime_error(std::format(
                                        "Ply Read Error: Property '{}' type mismatch. Expected SCALAR, but found LIST in file."
                                        , PI::property_name));

                                row_item = ply_cast<typename PI::ScalarType>(reader.readScalar(prop.valueKind));
                            }
                        };
                    }
                    else {
                        throw std::runtime_error(std::format(
                            "Ply Read Error: Element '{}' is missing required property '{}'."
                            , elem.name, PI::property_name));
                    }
                }(), ...);
            }(std::make_index_sequence<SpecT::property_num>{});
         }(specs), ...); 

        for (size_t ri = 0; ri < elem.count; ++ri) {
            for (auto& rd : columnReaders) rd(ri);
        }
    }
}

template <typename... Specs>
    requires (detail::IsPropertySpec<Specs> && ...)
void bind_writer(PlyStreamWriter& writer, const Specs&... specs) {
    static_assert(!detail::check_property_conflicts<Specs...>(),
        "Multiple specs bind to the SAME property of the SAME element.");

    std::vector<PlyElement> unique_elements;

    ([&]() {
        PlyElement new_elem = specs.create();

        auto it = std::find_if(unique_elements.begin(), unique_elements.end(),
            [&](const auto& elem) { return elem.name == new_elem.name; });

        if (it != unique_elements.end()) {
            if (it->count != new_elem.count)
                throw std::runtime_error(std::format(
                    "Ply Write Error: Element count mismatch for '{}'. All PropertySpecs for the same element must have the same size."
                    , new_elem.name));

            it->properties.insert(it->properties.end(),
                std::make_move_iterator(new_elem.properties.begin()),
                std::make_move_iterator(new_elem.properties.end()));
        }
        else {
            unique_elements.push_back(std::move(new_elem));
        }
        }(), ...);

    for (const auto& elem : unique_elements)
        writer.addElement(std::move(elem));

    writer.writeHeader();

    for (const auto& elem : unique_elements) {
        for (size_t ri = 0; ri < elem.count; ++ri) {

            ([&]() {
                using SpecT = std::decay_t<decltype(specs)>;
                if (SpecT::element_name == elem.name) {
                    const auto& row_view = specs()[ri];

                    [&] <size_t... Is>(std::index_sequence<Is...>) {
                        ([&]() {
                            using PI = typename SpecT::template ColumnInfo<Is>;
                            const auto& row_item = get<Is>(row_view);

                            if constexpr (PI::list_kind != ScalarKind::UNUSED) {
                                [&](auto& container) {
                                    size_t c_size = 0;
                                    if constexpr (requires { container.size(); })
                                        c_size = container.size(); 

                                    writer.writeScalar(static_cast<uint32_t>(c_size), PI::list_kind);
                                    for (const auto& v : container) 
                                        writer.writeScalar(v/*, PI::value_kind*/);
                                }(row_item);
                            }
                            else {
                                writer.writeScalar(row_item/*, PI::value_kind*/);
                            }
                        }(), ...);
                    }(std::make_index_sequence<SpecT::property_num>{});
                }
            }(), ...);

            writer.writeLineEnd();
        }
    }

    writer.flush();
}

}
