/**
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 TAO 12804985@qq.com
 *
 * @file    turboply.hpp
* @brief    TurboPLY: A lightweight, high-performance PLY file I/O library.
 *          Supports binary little-endian and ASCII formats, and provides
 *          optional memory-mapped file I/O for zero-copy, high-throughput
 *          access to large datasets. Big-endian format is intentionally 
 *          not supported for simplicity and performance.
 * 
 * @version 1.0.6
 * @date    2026-02-10
 *
 * -----------------------------------------------------------------------------
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * -----------------------------------------------------------------------------
 */

#pragma once

#include <span>
#include <functional>
#include <type_traits>

namespace turboply {

    template<typename T> 
    concept is_record_type_compatible = std::is_arithmetic_v<T> ||
        requires { typename T::value_type;
        requires std::is_arithmetic_v<typename T::value_type>;
    };

    template<typename... Types>
        requires (is_record_type_compatible<Types> && ...)
    struct RecordTuple {
    private:
        template<typename... Ts> struct Storage;
        template<typename T> struct Storage<T> { T first; auto operator<=>(const Storage&) const = default; };
        template<typename H, typename N, typename... T> struct Storage<H, N, T...> { H first; Storage<N, T...> rest; auto operator<=>(const Storage&) const = default; };

        template<std::size_t I, typename S> static constexpr auto& get_impl(S& s) {
            if constexpr (I == 0) return s.first; else return get_impl<I - 1>(s.rest);
        }
        template<std::size_t I, typename... Ts> struct type_at;
        template<typename H, typename... T> struct type_at<0, H, T...> { using type = H; };
        template<std::size_t I, typename H, typename... T> struct type_at<I, H, T...> { using type = typename type_at<I - 1, T...>::type; };

        Storage<Types...> storage;

    public:
        constexpr RecordTuple() = default;

        template<typename... Args> requires (sizeof...(Args) != 1 || !(std::is_same_v<std::decay_t<Args>, RecordTuple> && ...))
        constexpr explicit RecordTuple(Args&&... args) : storage{ std::forward<Args>(args)... } {}

        auto operator<=>(const RecordTuple&) const = default;
        static constexpr std::size_t size() { return sizeof...(Types); }
        template<std::size_t I> using field_type = typename type_at<I, Types...>::type;

        template<std::size_t I> constexpr auto& get()& { return get_impl<I>(storage); }
        template<std::size_t I> constexpr const auto& get() const& { return get_impl<I>(storage); }
        template<std::size_t I> constexpr auto&& get()&& { return std::move(get_impl<I>(storage)); }

        template<std::size_t N> friend constexpr auto& get(RecordTuple& t) { return t.template get<N>(); }
        template<std::size_t N> friend constexpr const auto& get(const RecordTuple& t) { return t.template get<N>(); }
    };

    template<> struct RecordTuple<> {
        constexpr RecordTuple() = default; auto operator<=>(const RecordTuple<>&) const = default;
    };

    //////////////////////////////////////////////////////////////////////////

    namespace detail {

        template<size_t N> struct fixed_string {
            char data[N];
            constexpr fixed_string(const char(&str)[N]) { std::copy_n(str, N, data); }
            auto operator<=>(const fixed_string&) const = default;
            constexpr operator const char* () const { return data; }
        };

        template <typename T1, typename T2>
        struct is_row_type_compatible_trait {
            static constexpr bool get_value() {
                if constexpr (std::is_same_v<std::remove_cvref_t<T1>, std::remove_cvref_t<T2>>) {
                    return true;
                }
                constexpr bool is_c1 = requires { typename T1::value_type; };
                constexpr bool is_c2 = requires { typename T2::value_type; };
                if constexpr (is_c1 && is_c2) {
                    using V1 = std::remove_cvref_t<typename T1::value_type>;
                    using V2 = std::remove_cvref_t<typename T2::value_type>;
                    if constexpr (std::is_arithmetic_v<V1> && std::is_arithmetic_v<V2>) {
                        return std::is_same_v<V1, V2>; // 内部为数值类型时强行要求类型完全一致
                    }
                    return is_row_type_compatible_trait<V1, V2>::value;
                }
                return true;
            }
            static constexpr bool value = get_value();
        };

        template <typename T1, typename T2> 
        concept is_row_type_compatible = is_row_type_compatible_trait<T1, T2>::value;

        template <fixed_string ElementName, typename RowT>
        struct PropertySpecBase {
            using RowType = RowT;
            using ColumnData = std::vector<RowType>;
            using ColumnView = std::span<RowType>;
            static constexpr std::string_view element_name{ ElementName };

            void bind_storage(ColumnData& d) {
                this->_column_view = ColumnView(d);
                this->_column_data = &d;
                this->_is_writable = true;
            }
            void bind_storage(const ColumnData& d) {
                _column_view = ColumnView(const_cast<RowType*>(d.data()), d.size());
                _column_data = nullptr;
                _is_writable = false;
            }
            void bind_storage(ColumnView v) {
                _column_view = v;
                _column_data = nullptr;
                _is_writable = true;
            }
            void bind_storage(std::span<const RowType> v) {
                _column_view = ColumnView(const_cast<RowType*>(v.data()), v.size());
                _column_data = nullptr;
                _is_writable = false;
            }

            template <typename U> requires (sizeof(U) == sizeof(RowType)) && is_row_type_compatible<U, RowType>
            void bind_storage(std::vector<U>& d) {
                bind_storage(reinterpret_cast<ColumnData&>(d));
            }
            template <typename U> requires (sizeof(U) == sizeof(RowType)) && is_row_type_compatible<U, RowType>
            void bind_storage(const std::vector<U>& d) {
                bind_storage(reinterpret_cast<const ColumnData&>(d));
            }
            template <typename U> requires (sizeof(U) == sizeof(RowType)) && is_row_type_compatible<U, RowType>
            void bind_storage(std::span<U> v) {
                bind_storage(ColumnView(reinterpret_cast<RowType*>(v.data()), v.size()));
            }

            PropertySpecBase(ColumnData& d) { bind_storage(d); }
            PropertySpecBase(const ColumnData& d) { bind_storage(d); }
            PropertySpecBase(ColumnView v) { bind_storage(v); }
            PropertySpecBase(std::span<const RowType> v) { bind_storage(v); }

            template <typename U> requires (sizeof(U) == sizeof(RowType)) && is_row_type_compatible<U, RowType>
            PropertySpecBase(std::vector<U>& d) { bind_storage(d); }
            template <typename U> requires (sizeof(U) == sizeof(RowType)) && is_row_type_compatible<U, RowType>
            PropertySpecBase(const std::vector<U>& d) { bind_storage(d); }
            template <typename U> requires (sizeof(U) == sizeof(RowType)) && is_row_type_compatible<U, RowType>
            PropertySpecBase(std::span<U> v) { bind_storage(v); }

            ColumnView& operator()() { return _column_view; }
            const ColumnView& operator()() const { return _column_view; }

            virtual void resize_storage(size_t n) {
                if (this->_column_data) {
                    this->_column_data->resize(n);
                    refresh_view();
                }
                else if (this->_column_view.size() != n)
                    throw std::runtime_error(std::format(
                        "Ply Error: Element '{}' expects {} rows, but storage has {}.",
                        element_name, n, this->_column_view.size()));
            }

            void refresh_view() const {
                if (this->_column_data) {
                    this->_column_view = ColumnView(*(this->_column_data));
                }
            }

            virtual PlyElement create_element() const = 0;
            virtual void reset_schema() {}
            virtual bool try_bind_property(const PlyElement::Property&, std::function<void(size_t)>&, PlyStreamReader&) = 0;
            virtual void write_row(PlyStreamWriter&, size_t) const = 0;

        protected:
            mutable ColumnView _column_view;
            ColumnData* _column_data;
            bool _is_writable = true;
        };

        template <fixed_string ElementName, typename RowT, fixed_string... PropertyNames>
        struct FixedPropertySpec : public PropertySpecBase<ElementName, RowT> {
            static constexpr bool is_fixed = true;
            static constexpr size_t property_num = sizeof...(PropertyNames);

            virtual PlyElement create_element() const override {
                PlyElement elem{ std::string(Base::element_name), Base::_column_view.size(), {} };
                [&]<size_t... Is>(std::index_sequence<Is...>) {
                    (elem.properties.push_back({ std::string(Info<Is>::name), Info<Is>::Traits::value_kind, Info<Is>::Traits::list_kind }), ...);
                }(std::make_index_sequence<property_num>{});

                return elem;
            }

            virtual bool try_bind_property(const PlyElement::Property& prop, std::function<void(size_t)>& read_func, PlyStreamReader& reader) override {
                if (!this->_is_writable)
                    throw std::runtime_error(std::format("Ply Error: Element '{}' is read-only.", Base::element_name));

                bool bound = false;

                [&]<size_t... Is>(std::index_sequence<Is...>) {
                    ([&]() {
                        if (bound) return;
                        using Info = Info<Is>;
                        if (prop.name == Info::name) {
                            // Type check
                            if (Info::Traits::list_kind != ScalarKind::UNUSED && prop.listKind == ScalarKind::UNUSED)
                                throw std::runtime_error(std::format("Ply Error: Property '{}' expected LIST.", Info::name));
                            if (Info::Traits::list_kind == ScalarKind::UNUSED && prop.listKind != ScalarKind::UNUSED)
                                throw std::runtime_error(std::format("Ply Error: Property '{}' expected SCALAR.", Info::name));

                            bound = true;
                            read_func = [this, &reader, &prop](size_t row_idx) {
                                auto& val = get<Is>(this->_column_view[row_idx]);
                                if constexpr (Info::Traits::list_kind != ScalarKind::UNUSED) {
                                    size_t n = ply_cast<size_t>(reader.readScalar(prop.listKind));
                                    if constexpr (requires { val.resize(n); })
                                        val.resize(n);

                                    size_t lim = 0;
                                    if constexpr (requires { val.size(); })
                                        lim = std::min(n, val.size());

                                    for (size_t k = 0; k < lim; ++k)
                                        val[k] = ply_cast<typename Info::Traits::ScalarType>(reader.readScalar(prop.valueKind));
                                    for (size_t k = lim; k < n; ++k)
                                        reader.readScalar(prop.valueKind);
                                }
                                else
                                    val = ply_cast<typename Info::Traits::ScalarType>(reader.readScalar(prop.valueKind));
                                };
                        }
                        }(), ...);
                }(std::make_index_sequence<property_num>{});

                return bound;
            }

            virtual void write_row(PlyStreamWriter& writer, size_t row_idx) const override {
                const auto& row = Base::_column_view[row_idx];
                [&]<size_t... Is>(std::index_sequence<Is...>) {
                    ([&]() {
                        using Info = Info<Is>;
                        const auto& val = get<Is>(row);
                        if constexpr (Info::Traits::list_kind != ScalarKind::UNUSED) {
                            size_t sz = 0;
                            if constexpr (requires { val.size(); })
                                sz = val.size();

                            writer.writeScalar((uint32_t)sz, Info::Traits::list_kind);
                            for (const auto& v : val)
                                writer.writeScalar(v);
                        }
                        else
                            writer.writeScalar(val);
                        }(), ...);
                }(std::make_index_sequence<property_num>{});
            }

        private:
            using Base = PropertySpecBase<ElementName, RowT>;
            using Base::Base;

            template <typename T>
            struct column_traits {
                static constexpr ScalarKind kind() {
                    if constexpr (std::is_same_v<T, float>)    return ScalarKind::FLOAT32;
                    if constexpr (std::is_same_v<T, double>)   return ScalarKind::FLOAT64;
                    if constexpr (std::is_same_v<T, int32_t>)  return ScalarKind::INT32;
                    if constexpr (std::is_same_v<T, uint32_t>) return ScalarKind::UINT32;
                    if constexpr (std::is_same_v<T, int16_t>)  return ScalarKind::INT16;
                    if constexpr (std::is_same_v<T, uint16_t>) return ScalarKind::UINT16;
                    if constexpr (std::is_same_v<T, uint8_t>)  return ScalarKind::UINT8;
                    if constexpr (std::is_same_v<T, int8_t>)   return ScalarKind::INT8;
                    if constexpr (std::is_same_v<T, char>)     return ScalarKind::INT8;
                    return ScalarKind::UNUSED;
                }
                using ScalarType = T;
                static constexpr ScalarKind value_kind = kind();
                static constexpr ScalarKind list_kind = ScalarKind::UNUSED;
            };

            template <typename T, typename A>
            struct column_traits<std::vector<T, A>> {
                using ScalarType = T;
                static constexpr ScalarKind value_kind = column_traits<T>::kind();
                static constexpr ScalarKind list_kind = ScalarKind::UINT8/*UINT32*/; // TODO meshlab VCGlib bug no support without uint8
            };

            template <typename T, size_t N>
            struct column_traits<std::array<T, N>> {
                using ScalarType = T;
                static constexpr ScalarKind value_kind = column_traits<T>::kind();
                static constexpr ScalarKind list_kind = ScalarKind::UINT8;
            };

            template <size_t I>
            struct Info {
                static constexpr auto __name = std::get<I>(std::tuple{ PropertyNames... });
                static constexpr std::string_view name{ __name };
                using FieldT = typename RowT::template field_type<I>;
                using Traits = column_traits<FieldT>;
            };
        };

        template <fixed_string ElementName, typename ScalarT = float> requires std::is_arithmetic_v<ScalarT>
        struct DynamicPropertySpec : public PropertySpecBase<ElementName, std::vector<ScalarT>> {
            using Base = PropertySpecBase<ElementName, std::vector<ScalarT>>;
            using Base::Base;
            static constexpr bool is_fixed = false;

            std::vector<PlyElement::Property> properties;

            virtual PlyElement create_element() const override {
                return PlyElement{ std::string(Base::element_name), Base::_column_view.size(), properties };
            }

            virtual void reset_schema() override {
                properties.clear();
            }

            virtual void resize_storage(size_t n) override {
                Base::resize_storage(n);
                size_t col_num = properties.size();
                for (auto& row : this->_column_view) {
                    if (row.size() < col_num) {
                        row.resize(col_num);
                    }
                }
            }

            virtual bool try_bind_property(const PlyElement::Property& prop, std::function<void(size_t)>& read_func, PlyStreamReader& reader) override {
                if (!this->_is_writable)
                    throw std::runtime_error(std::format("Ply Error: Element '{}' is read-only.", Base::element_name));
                if (prop.listKind != ScalarKind::UNUSED) return false; // Dynamic list not supported yet

                properties.push_back(prop);
                size_t col_idx = properties.size() - 1;

                typename Base::refresh_view();

                read_func = [this, &reader, col_idx, &prop](size_t row_idx) {
                    auto& row = this->_column_view[row_idx];
                    row[col_idx] = ply_cast<ScalarT>(reader.readScalar(prop.valueKind));
                    };

                return true;
            }

            virtual void write_row(PlyStreamWriter& writer, size_t row_idx) const override {
                const auto& row = Base::_column_view[row_idx];

                if (row.size() < properties.size()) {
                    throw std::runtime_error(std::format(
                        "Ply Write Error: Row {} in element '{}' has size {} which is less than the expected properties count of {}.",
                        row_idx, Base::element_name, row.size(), properties.size()));
                }

                for (size_t i = 0; i < properties.size(); ++i)
                    writer.writeScalar(row[i], properties[i].valueKind);
            }
        };

        template <typename T> 
        concept is_property_spec = requires(T t) {
            t.create_element();
            { T::is_fixed } -> std::convertible_to<bool>;
        };

        template <typename T, typename Seq>
        struct repeat_record_impl;

        template <typename T, size_t... Is>
        struct repeat_record_impl<T, std::index_sequence<Is...>> {
            template <size_t> using type_mapper = T;
            using type = RecordTuple<type_mapper<Is>...>;
        };

        template <typename T, size_t N>
        using repeat_record_t = typename repeat_record_impl<std::remove_cvref_t<T>, std::make_index_sequence<N>>::type;

    }

    //////////////////////////////////////////////////////////////////////////

    template <detail::fixed_string ElementName, typename T, detail::fixed_string... PropertyNames>
    using UniformSpec = detail::FixedPropertySpec<ElementName, detail::repeat_record_t<T, sizeof...(PropertyNames)>, PropertyNames...>;

    template <detail::fixed_string ElementName, typename T, detail::fixed_string PropertyName>
    using ScalarSpec = UniformSpec<ElementName, T, PropertyName>;

    template <detail::fixed_string ElementName, typename T, detail::fixed_string PropertyName, size_t Len = 0>
    using ListSpec = detail::FixedPropertySpec<ElementName, RecordTuple<std::conditional_t<Len == 0, std::vector<T>, std::array<T, Len>>>, PropertyName>;

    template <detail::fixed_string Name, typename T, detail::fixed_string... PropertyNames>
    using CustomSpec = detail::FixedPropertySpec<Name, T, PropertyNames...>;

    template <detail::fixed_string ElementName, typename ScalarT = float>
    using OtherSpec = detail::DynamicPropertySpec<ElementName, ScalarT>;

    using VertexSpec = UniformSpec<"vertex", float, "x", "y", "z">;
    using NormalSpec = UniformSpec<"vertex", float, "nx", "ny", "nz">;
    using ColorSpec  = UniformSpec<"vertex", uint8_t, "red", "green", "blue">;
    using FaceSpec   = ListSpec<"face", uint32_t, "vertex_indices", 3>;

    namespace meshlab {
        using UVSpec = ListSpec<"face", float, "texcoord", 6>;
        using TexnumberSpec = ScalarSpec<"face", int, "texnumber">;
    }

    //////////////////////////////////////////////////////////////////////////

    template <typename... Specs> requires (detail::is_property_spec<Specs> && ...)
        void bind_reader(PlyStreamReader& reader, Specs&... specs) {
        static_assert(((Specs::is_fixed ? 0 : 1) + ...) <= 1, "Ply Read Error: Multiple dynamic specs are not allowed in a single bind_reader call.");

        (specs.reset_schema(), ...);

        reader.parseHeader();

        for (const auto& elem : reader.getElements()) {
            if (elem.count == 0) continue;

            std::vector<std::function<void(size_t)>> reader_funcs(elem.properties.size());
            bool spec_bound[sizeof...(Specs)] = {};

            for (size_t i = 0; i < elem.properties.size(); ++i) {
                const auto& prop = elem.properties[i];
                bool claimed = false;

                [&] <size_t... Is>(std::index_sequence<Is...>) {
                    auto check_specs = [&]<size_t Index, bool IsFixedPass>(auto& spec) {
                        if (claimed) return;
                        using T = std::decay_t<decltype(spec)>;

                        if constexpr (T::is_fixed == IsFixedPass) {
                            if (spec.element_name == elem.name) {
                                if (spec.try_bind_property(prop, reader_funcs[i], reader)) {
                                    claimed = true;
                                    spec_bound[Index] = true;
                                }
                            }
                        }
                    };

                    (check_specs.template operator() < Is, true > (specs), ...);
                    (check_specs.template operator() < Is, false > (specs), ...);
                }(std::make_index_sequence<sizeof...(Specs)>{});

                if (!claimed) {
                    reader_funcs[i] = [&reader, prop](size_t) {
                        if (prop.listKind != ScalarKind::UNUSED) {
                            auto n = ply_cast<uint32_t>(reader.readScalar(prop.listKind));
                            for (uint32_t k = 0; k < n; ++k)
                                reader.readScalar(prop.valueKind);
                        }
                        else
                            reader.readScalar(prop.valueKind);
                        };
                }
            }

            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ([&]() {
                    if (spec_bound[Is]) {
                        auto& spec = std::get<Is>(std::forward_as_tuple(specs...));
                        spec.resize_storage(elem.count);
                    }
                    }(), ...);
            }(std::make_index_sequence<sizeof...(Specs)>{});

            for (size_t ri = 0; ri < elem.count; ++ri)
                for (auto& fn : reader_funcs) fn(ri);
        }
    }

    template <typename... Specs> requires (detail::is_property_spec<Specs> && ...)
        void bind_writer(PlyStreamWriter& writer, const Specs&... specs) {
        std::vector<PlyElement> merged_elements;

        auto get_element = [&](const std::string& name, size_t count) -> PlyElement& {
            for (auto& e : merged_elements)
                if (e.name == name)
                    return e;
            merged_elements.push_back({ name, count, {} });
            return merged_elements.back();
            };

        ([&]() {
            specs.refresh_view(); // 刷新视图
            PlyElement elem = specs.create_element();
            PlyElement& target = get_element(elem.name, elem.count);
            if (target.count != elem.count)
                throw std::runtime_error("Ply Write: Count mismatch for element " + elem.name);
            target.properties.insert(target.properties.end(), elem.properties.begin(), elem.properties.end());
            }(), ...);

        for (const auto& e : merged_elements) writer.addElement(e);
        writer.writeHeader();

        for (const auto& elem : merged_elements) {
            for (size_t ri = 0; ri < elem.count; ++ri) {
                ([&]() { if (specs.element_name == elem.name) specs.write_row(writer, ri); }(), ...);
                writer.writeLineEnd();
            }
        }
        writer.flush();
    }

}

template<typename... Ts> struct std::tuple_size<turboply::RecordTuple<Ts...>> : std::integral_constant<size_t, sizeof...(Ts)> {};
template<size_t I, typename... Ts> struct std::tuple_element<I, turboply::RecordTuple<Ts...>> {
    using type = typename turboply::RecordTuple<Ts...>::template field_type<I>;
};
