#pragma once

#include <DB/Core/FieldVisitors.h>

#include <DB/DataTypes/DataTypeArray.h>
#include <DB/DataTypes/DataTypesNumberFixed.h>
#include <DB/DataTypes/DataTypeDate.h>
#include <DB/DataTypes/DataTypeDateTime.h>
#include <DB/DataTypes/DataTypeString.h>

#include <DB/Columns/ColumnArray.h>
#include <DB/Columns/ColumnString.h>
#include <DB/Columns/ColumnTuple.h>

#include <DB/Functions/IFunction.h>
#include <DB/Functions/Conditional/CondException.h>
#include <DB/Common/HashTable/HashMap.h>
#include <DB/Common/HashTable/ClearableHashMap.h>
#include <DB/Common/StringUtils.h>
#include <DB/Interpreters/AggregationCommon.h>
#include <DB/Functions/FunctionsConditional.h>
#include <DB/Functions/FunctionsConversion.h>
#include <DB/Functions/Conditional/getArrayType.h>
#include <DB/AggregateFunctions/IAggregateFunction.h>
#include <DB/AggregateFunctions/AggregateFunctionFactory.h>
#include <DB/Parsers/ExpressionListParsers.h>
#include <DB/Parsers/parseQuery.h>
#include <DB/Parsers/ASTExpressionList.h>
#include <DB/Parsers/ASTLiteral.h>

#include <ext/range.hpp>

#include <unordered_map>
#include <numeric>


namespace DB
{

namespace ErrorCodes
{
	extern const int ZERO_ARRAY_OR_TUPLE_INDEX;
	extern const int SIZES_OF_ARRAYS_DOESNT_MATCH;
	extern const int PARAMETERS_TO_AGGREGATE_FUNCTIONS_MUST_BE_LITERALS;
}


/** Функции по работе с массивами:
  *
  * array(с1, с2, ...) - создать массив из констант.
  * arrayElement(arr, i) - получить элемент массива по индексу.
  *  Индекс начинается с 1. Также индекс может быть отрицательным - тогда он считается с конца массива.
  * has(arr, x) - есть ли в массиве элемент x.
  * indexOf(arr, x) - возвращает индекс элемента x (начиная с 1), если он есть в массиве, или 0, если его нет.
  * arrayEnumerate(arr) - возаращает массив [1,2,3,..., length(arr)]
  *
  * arrayUniq(arr) - считает количество разных элементов в массиве,
  * arrayUniq(arr1, arr2, ...) - считает количество разных кортежей из элементов на соответствующих позициях в нескольких массивах.
  *
  * arrayEnumerateUniq(arr)
  *  - возаращает массив,  параллельный данному, где для каждого элемента указано,
  *  какой он по счету среди элементов с таким значением.
  *  Например: arrayEnumerateUniq([10, 20, 10, 30]) = [1,  1,  2,  1]
  * arrayEnumerateUniq(arr1, arr2...)
  *  - для кортежей из элементов на соответствующих позициях в нескольких массивах.
  *
  * emptyArrayToSingle(arr) - заменить пустые массивы на массивы из одного элемента со значением "по-умолчанию".
  *
  * arrayReduce('agg', arr1, ...) - применить агрегатную функцию agg к массивам arr1...
  */


class FunctionArray : public IFunction
{
public:
	static constexpr auto name = "array";
	static FunctionPtr create(const Context & context);

	FunctionArray(const Context & context);

	void setCaseMode();

	/// Получить тип результата по типам аргументов. Если функция неприменима для данных аргументов - кинуть исключение.
	DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override;

	/// Выполнить функцию над блоком.
	void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override;

private:
	/// Получить имя функции.
	String getName() const override;

	bool addField(DataTypePtr type_res, const Field & f, Array & arr) const;
	static const DataTypePtr & getScalarType(const DataTypePtr & type);
	DataTypeTraits::EnrichedDataTypePtr getLeastCommonType(const DataTypes & arguments) const;

private:
	const Context & context;
	bool is_case_mode = false;
};



class FunctionArrayElement : public IFunction
{
public:
	static constexpr auto name = "arrayElement";
	static FunctionPtr create(const Context & context);

	/// Получить имя функции.
	String getName() const override;

	/// Получить типы результата по типам аргументов. Если функция неприменима для данных аргументов - кинуть исключение.
	DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override;

	/// Выполнить функцию над блоком.
	void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override;

private:
	template <typename DataType>
	bool executeNumberConst(Block & block, const ColumnNumbers & arguments, size_t result, const Field & index);

	template <typename IndexType, typename DataType>
	bool executeNumber(Block & block, const ColumnNumbers & arguments, size_t result, const PaddedPODArray<IndexType> & indices);

	bool executeStringConst(Block & block, const ColumnNumbers & arguments, size_t result, const Field & index);

	template <typename IndexType>
	bool executeString(Block & block, const ColumnNumbers & arguments, size_t result, const PaddedPODArray<IndexType> & indices);

	bool executeGenericConst(Block & block, const ColumnNumbers & arguments, size_t result, const Field & index);

	template <typename IndexType>
	bool executeGeneric(Block & block, const ColumnNumbers & arguments, size_t result, const PaddedPODArray<IndexType> & indices);

	bool executeConstConst(Block & block, const ColumnNumbers & arguments, size_t result, const Field & index);

	template <typename IndexType>
	bool executeConst(Block & block, const ColumnNumbers & arguments, size_t result, const PaddedPODArray<IndexType> & indices);

	template <typename IndexType>
	bool executeArgument(Block & block, const ColumnNumbers & arguments, size_t result);

	/** Для массива кортежей функция вычисляется покомпонентно - для каждого элемента кортежа.
	  */
	bool executeTuple(Block & block, const ColumnNumbers & arguments, size_t result);
};


/// For has.
struct IndexToOne
{
	using ResultType = UInt8;
	static bool apply(size_t j, ResultType & current) { current = 1; return false; }
};

/// For indexOf.
struct IndexIdentity
{
	using ResultType = UInt64;
	/// Индекс возвращается начиная с единицы.
	static bool apply(size_t j, ResultType & current) { current = j + 1; return false; }
};

/// For countEqual.
struct IndexCount
{
	using ResultType = UInt32;
	static bool apply(size_t j, ResultType & current) { ++current; return true; }
};


template <typename T, typename U, typename IndexConv>
struct ArrayIndexNumImpl
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"

	/// compares `lhs` against `i`-th element of `rhs`
	static bool compare(const T & lhs, const PaddedPODArray<U> & rhs, const std::size_t i ) { return lhs == rhs[i]; }
	/// compares `lhs against `rhs`, third argument unused
	static bool compare(const T & lhs, const U & rhs, std::size_t) { return lhs == rhs; }

#pragma GCC diagnostic pop

	template <typename ScalarOrVector>
	static void vector(
		const PaddedPODArray<T> & data, const ColumnArray::Offsets_t & offsets,
		const ScalarOrVector & value,
		PaddedPODArray<typename IndexConv::ResultType> & result)
	{
		size_t size = offsets.size();
		result.resize(size);

		ColumnArray::Offset_t current_offset = 0;
		for (size_t i = 0; i < size; ++i)
		{
			size_t array_size = offsets[i] - current_offset;
			typename IndexConv::ResultType current = 0;

			for (size_t j = 0; j < array_size; ++j)
				if (compare(data[current_offset + j], value, i))
					if (!IndexConv::apply(j, current))
						break;

			result[i] = current;
			current_offset = offsets[i];
		}
	}
};

template <typename IndexConv>
struct ArrayIndexStringImpl
{
	static void vector_const(
		const ColumnString::Chars_t & data, const ColumnArray::Offsets_t & offsets, const ColumnString::Offsets_t & string_offsets,
		const String & value,
		PaddedPODArray<typename IndexConv::ResultType> & result)
	{
		const auto size = offsets.size();
		const auto value_size = value.size();
		result.resize(size);

		ColumnArray::Offset_t current_offset = 0;
		for (size_t i = 0; i < size; ++i)
		{
			const auto array_size = offsets[i] - current_offset;
			typename IndexConv::ResultType current = 0;

			for (size_t j = 0; j < array_size; ++j)
			{
				ColumnArray::Offset_t string_pos = current_offset == 0 && j == 0
					? 0
					: string_offsets[current_offset + j - 1];

				ColumnArray::Offset_t string_size = string_offsets[current_offset + j] - string_pos;

				if (string_size == value_size + 1 && 0 == memcmp(value.data(), &data[string_pos], value_size))
				{
					if (!IndexConv::apply(j, current))
						break;
				}
			}

			result[i] = current;
			current_offset = offsets[i];
		}
	}

	static void vector_vector(
		const ColumnString::Chars_t & data, const ColumnArray::Offsets_t & offsets, const ColumnString::Offsets_t & string_offsets,
		const ColumnString::Chars_t & item_values, const ColumnString::Offsets_t & item_offsets,
		PaddedPODArray<typename IndexConv::ResultType> & result)
	{
		const auto size = offsets.size();
		result.resize(size);

		ColumnArray::Offset_t current_offset = 0;
		for (size_t i = 0; i < size; ++i)
		{
			const auto array_size = offsets[i] - current_offset;
			typename IndexConv::ResultType current = 0;
			const auto value_pos = 0 == i ? 0 : item_offsets[i - 1];
			const auto value_size = item_offsets[i] - value_pos;

			for (size_t j = 0; j < array_size; ++j)
			{
				ColumnArray::Offset_t string_pos = current_offset == 0 && j == 0
												   ? 0
												   : string_offsets[current_offset + j - 1];

				ColumnArray::Offset_t string_size = string_offsets[current_offset + j] - string_pos;

				if (string_size == value_size && 0 == memcmp(&item_values[value_pos], &data[string_pos], value_size))
				{
					if (!IndexConv::apply(j, current))
						break;
				}
			}

			result[i] = current;
			current_offset = offsets[i];
		}
	}
};

/** Catch-all implementation for arrays of arbitary type.
  */
template <typename IndexConv, bool is_value_has_single_element_to_compare>
struct ArrayIndexGenericImpl
{
	/** To compare with constant value, create non-constant column with single element,
	  *  and pass is_value_has_single_element_to_compare = true.
	  */

	static void vector(
		const IColumn & data, const ColumnArray::Offsets_t & offsets,
		const IColumn & value,
		PaddedPODArray<typename IndexConv::ResultType> & result)
	{
		size_t size = offsets.size();
		result.resize(size);

		ColumnArray::Offset_t current_offset = 0;
		for (size_t i = 0; i < size; ++i)
		{
			size_t array_size = offsets[i] - current_offset;
			typename IndexConv::ResultType current = 0;

			for (size_t j = 0; j < array_size; ++j)
				if (0 == data.compareAt(current_offset + j, is_value_has_single_element_to_compare ? 0 : i, value, 1))
					if (!IndexConv::apply(j, current))
						break;

			result[i] = current;
			current_offset = offsets[i];
		}
	}
};


template <typename IndexConv, typename Name>
class FunctionArrayIndex : public IFunction
{
public:
	static constexpr auto name = Name::name;
	static FunctionPtr create(const Context & context) { return std::make_shared<FunctionArrayIndex>(); }

private:
	using ResultColumnType = ColumnVector<typename IndexConv::ResultType>;

	template <typename T>
	bool executeNumber(Block & block, const ColumnNumbers & arguments, size_t result)
	{
		return executeNumberNumber<T, UInt8>(block, arguments, result)
			|| executeNumberNumber<T, UInt16>(block, arguments, result)
			|| executeNumberNumber<T, UInt32>(block, arguments, result)
			|| executeNumberNumber<T, UInt64>(block, arguments, result)
			|| executeNumberNumber<T, Int8>(block, arguments, result)
			|| executeNumberNumber<T, Int16>(block, arguments, result)
			|| executeNumberNumber<T, Int32>(block, arguments, result)
			|| executeNumberNumber<T, Int64>(block, arguments, result)
			|| executeNumberNumber<T, Float32>(block, arguments, result)
			|| executeNumberNumber<T, Float64>(block, arguments, result);
	}

	template <typename T, typename U>
	bool executeNumberNumber(Block & block, const ColumnNumbers & arguments, size_t result)
	{
		const ColumnArray * col_array = typeid_cast<const ColumnArray *>(block.getByPosition(arguments[0]).column.get());

		if (!col_array)
			return false;

		const ColumnVector<T> * col_nested = typeid_cast<const ColumnVector<T> *>(&col_array->getData());

		if (!col_nested)
			return false;

		const auto item_arg = block.getByPosition(arguments[1]).column.get();

		if (const auto item_arg_const = typeid_cast<const ColumnConst<U> *>(item_arg))
		{
			const auto col_res = std::make_shared<ResultColumnType>();
			block.getByPosition(result).column = col_res;

			ArrayIndexNumImpl<T, U, IndexConv>::vector(col_nested->getData(), col_array->getOffsets(),
				item_arg_const->getData(), col_res->getData());
		}
		else if (const auto item_arg_vector = typeid_cast<const ColumnVector<U> *>(item_arg))
		{
			const auto col_res = std::make_shared<ResultColumnType>();
			block.getByPosition(result).column = col_res;

			ArrayIndexNumImpl<T, U, IndexConv>::vector(col_nested->getData(), col_array->getOffsets(),
				item_arg_vector->getData(), col_res->getData());
		}
		else
			return false;

		return true;
	}

	bool executeString(Block & block, const ColumnNumbers & arguments, size_t result)
	{
		const ColumnArray * col_array = typeid_cast<const ColumnArray *>(block.getByPosition(arguments[0]).column.get());

		if (!col_array)
			return false;

		const ColumnString * col_nested = typeid_cast<const ColumnString *>(&col_array->getData());

		if (!col_nested)
			return false;

		const auto item_arg = block.getByPosition(arguments[1]).column.get();

		if (const auto item_arg_const = typeid_cast<const ColumnConst<String> *>(item_arg))
		{
			const auto col_res = std::make_shared<ResultColumnType>();
			block.getByPosition(result).column = col_res;

			ArrayIndexStringImpl<IndexConv>::vector_const(col_nested->getChars(), col_array->getOffsets(),
				col_nested->getOffsets(), item_arg_const->getData(), col_res->getData());
		}
		else if (const auto item_arg_vector = typeid_cast<const ColumnString *>(item_arg))
		{
			const auto col_res = std::make_shared<ResultColumnType>();
			block.getByPosition(result).column = col_res;

			ArrayIndexStringImpl<IndexConv>::vector_vector(col_nested->getChars(), col_array->getOffsets(),
				col_nested->getOffsets(), item_arg_vector->getChars(), item_arg_vector->getOffsets(),
				col_res->getData());
		}
		else
			return false;

		return true;
	}

	bool executeConst(Block & block, const ColumnNumbers & arguments, size_t result)
	{
		const ColumnConstArray * col_array = typeid_cast<const ColumnConstArray *>(block.getByPosition(arguments[0]).column.get());

		if (!col_array)
			return false;

		const Array & arr = col_array->getData();

		const auto item_arg = block.getByPosition(arguments[1]).column.get();
		if (item_arg->isConst())
		{
			typename IndexConv::ResultType current = 0;
			const auto & value = (*item_arg)[0];

			for (size_t i = 0, size = arr.size(); i < size; ++i)
			{
				if (apply_visitor(FieldVisitorAccurateEquals(), arr[i], value))
				{
					if (!IndexConv::apply(i, current))
						break;
				}
			}

			block.getByPosition(result).column = block.getByPosition(result).type->createConstColumn(
				item_arg->size(),
				static_cast<typename NearestFieldType<typename IndexConv::ResultType>::Type>(current));
		}
		else
		{
			const auto size = item_arg->size();
			const auto col_res = std::make_shared<ResultColumnType>(size);
			block.getByPosition(result).column = col_res;

			auto & data = col_res->getData();

			for (size_t row = 0; row < size; ++row)
			{
				const auto & value = (*item_arg)[row];

				data[row] = 0;
				for (size_t i = 0, size = arr.size(); i < size; ++i)
					if (apply_visitor(FieldVisitorAccurateEquals(), arr[i], value))
						if (!IndexConv::apply(i, data[row]))
							break;
			}
		}

		return true;
	}

	bool executeGeneric(Block & block, const ColumnNumbers & arguments, size_t result)
	{
		const ColumnArray * col_array = typeid_cast<const ColumnArray *>(block.getByPosition(arguments[0]).column.get());

		if (!col_array)
			return false;

		const IColumn & col_nested = col_array->getData();
		const IColumn & item_arg = *block.getByPosition(arguments[1]).column;

		const auto col_res = std::make_shared<ResultColumnType>();
		block.getByPosition(result).column = col_res;

		if (item_arg.isConst())
		{
			ArrayIndexGenericImpl<IndexConv, true>::vector(col_nested, col_array->getOffsets(),
				*item_arg.cut(0, 1)->convertToFullColumnIfConst(), col_res->getData());
		}
		else
		{
			/// If item_arg is tuple and have constants.
			if (auto materialized_tuple = item_arg.convertToFullColumnIfConst())
			{
				ArrayIndexGenericImpl<IndexConv, false>::vector(
					col_nested, col_array->getOffsets(), *materialized_tuple, col_res->getData());
			}
			else
				ArrayIndexGenericImpl<IndexConv, false>::vector(
					col_nested, col_array->getOffsets(), item_arg, col_res->getData());
		}

		return true;
	}


public:
	/// Получить имя функции.
	String getName() const override
	{
		return name;
	}

	/// Получить типы результата по типам аргументов. Если функция неприменима для данных аргументов - кинуть исключение.
	DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
	{
		if (arguments.size() != 2)
			throw Exception("Number of arguments for function " + getName() + " doesn't match: passed "
				+ toString(arguments.size()) + ", should be 2.",
				ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

		const DataTypeArray * array_type = typeid_cast<const DataTypeArray *>(arguments[0].get());
		if (!array_type)
			throw Exception("First argument for function " + getName() + " must be array.", ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

		if (!(array_type->getNestedType()->behavesAsNumber() && arguments[1]->behavesAsNumber())
			&& array_type->getNestedType()->getName() != arguments[1]->getName())
			throw Exception("Type of array elements and second argument for function " + getName() + " must be same."
				" Passed: " + arguments[0]->getName() + " and " + arguments[1]->getName() + ".", ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

		return std::make_shared<typename DataTypeFromFieldType<typename IndexConv::ResultType>::Type>();
	}

	/// Выполнить функцию над блоком.
	void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
	{
		if (!(executeNumber<UInt8>(block, arguments, result)
			|| executeNumber<UInt16>(block, arguments, result)
			|| executeNumber<UInt32>(block, arguments, result)
			|| executeNumber<UInt64>(block, arguments, result)
			|| executeNumber<Int8>(block, arguments, result)
			|| executeNumber<Int16>(block, arguments, result)
			|| executeNumber<Int32>(block, arguments, result)
			|| executeNumber<Int64>(block, arguments, result)
			|| executeNumber<Float32>(block, arguments, result)
			|| executeNumber<Float64>(block, arguments, result)
			|| executeConst(block, arguments, result)
			|| executeString(block, arguments, result)
			|| executeGeneric(block, arguments, result)))
			throw Exception{
				"Illegal column " + block.getByPosition(arguments[0]).column->getName()
				+ " of first argument of function " + getName(),
				ErrorCodes::ILLEGAL_COLUMN};
	}
};


class FunctionArrayEnumerate : public IFunction
{
public:
	static constexpr auto name = "arrayEnumerate";
	static FunctionPtr create(const Context & context);

	/// Получить имя функции.
	String getName() const override;

	/// Получить типы результата по типам аргументов. Если функция неприменима для данных аргументов - кинуть исключение.
	DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override;

	/// Выполнить функцию над блоком.
	void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override;
};


/// Считает количество разных элементов в массиве, или количество разных кортежей из элементов на соответствующих позициях в нескольких массивах.
/// NOTE Реализация частично совпадает с arrayEnumerateUniq.
class FunctionArrayUniq : public IFunction
{
public:
	static constexpr auto name = "arrayUniq";
	static FunctionPtr create(const Context & context);

	/// Получить имя функции.
	String getName() const override;

	/// Получить типы результата по типам аргументов. Если функция неприменима для данных аргументов - кинуть исключение.
	DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override;

	/// Выполнить функцию над блоком.
	void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override;

private:
	/// Изначально выделить кусок памяти для 512 элементов.
	static constexpr size_t INITIAL_SIZE_DEGREE = 9;

	template <typename T>
	bool executeNumber(const ColumnArray * array, ColumnUInt32::Container_t & res_values);

	bool executeString(const ColumnArray * array, ColumnUInt32::Container_t & res_values);

	bool executeConst(Block & block, const ColumnNumbers & arguments, size_t result);

	bool execute128bit(
		const ColumnArray::Offsets_t & offsets,
		const ConstColumnPlainPtrs & columns,
		ColumnUInt32::Container_t & res_values);

	void executeHashed(
		const ColumnArray::Offsets_t & offsets,
		const ConstColumnPlainPtrs & columns,
		ColumnUInt32::Container_t & res_values);
};


class FunctionArrayEnumerateUniq : public IFunction
{
public:
	static constexpr auto name = "arrayEnumerateUniq";
	static FunctionPtr create(const Context & context);

	/// Получить имя функции.
	String getName() const override;

	/// Получить типы результата по типам аргументов. Если функция неприменима для данных аргументов - кинуть исключение.
	DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override;

	/// Выполнить функцию над блоком.
	void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override;

private:
	/// Изначально выделить кусок памяти для 512 элементов.
	static constexpr size_t INITIAL_SIZE_DEGREE = 9;

	template <typename T>
	bool executeNumber(const ColumnArray * array, ColumnUInt32::Container_t & res_values);

	bool executeString(const ColumnArray * array, ColumnUInt32::Container_t & res_values);

	bool executeConst(Block & block, const ColumnNumbers & arguments, size_t result);

	bool execute128bit(
		const ColumnArray::Offsets_t & offsets,
		const ConstColumnPlainPtrs & columns,
		ColumnUInt32::Container_t & res_values);

	void executeHashed(
		const ColumnArray::Offsets_t & offsets,
		const ConstColumnPlainPtrs & columns,
		ColumnUInt32::Container_t & res_values);
};


template <typename Type> struct TypeToColumnType { using ColumnType = ColumnVector<Type>; };
template <> struct TypeToColumnType<String> { using ColumnType = ColumnString; };

template <typename DataType> struct DataTypeToName : TypeName<typename DataType::FieldType> { };
template <> struct DataTypeToName<DataTypeDate> { static std::string get() { return "Date"; } };
template <> struct DataTypeToName<DataTypeDateTime> { static std::string get() { return "DateTime"; } };

template <typename DataType>
struct FunctionEmptyArray : public IFunction
{
	static constexpr auto base_name = "emptyArray";
	static const String name;
	static FunctionPtr create(const Context & context) { return std::make_shared<FunctionEmptyArray>(); }

private:
	String getName() const override
	{
		return name;
	}

	DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
	{
		if (arguments.size() != 0)
			throw Exception("Number of arguments for function " + getName() + " doesn't match: passed "
				+ toString(arguments.size()) + ", should be 0.",
				ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

		return std::make_shared<DataTypeArray>(std::make_shared<DataType>());
	}

	void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
	{
		using UnderlyingColumnType = typename TypeToColumnType<typename DataType::FieldType>::ColumnType;

		block.getByPosition(result).column = std::make_shared<ColumnArray>(
			std::make_shared<UnderlyingColumnType>(),
			std::make_shared<ColumnArray::ColumnOffsets_t>(block.rowsInFirstColumn(), 0));
	}
};

template <typename DataType>
const String FunctionEmptyArray<DataType>::name = FunctionEmptyArray::base_name + DataTypeToName<DataType>::get();

class FunctionRange : public IFunction
{
public:
	static constexpr auto max_elements = 100000000;
	static constexpr auto name = "range";
	static FunctionPtr create(const Context &) { return std::make_shared<FunctionRange>(); }

private:
	String getName() const override
	{
		return name;
	}

	DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
	{
		if (arguments.size() != 1)
			throw Exception{
				"Number of arguments for function " + getName() + " doesn't match: passed "
				+ toString(arguments.size()) + ", should be 1.",
				ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH
			};

		const auto arg = arguments.front().get();

		if (!typeid_cast<const DataTypeUInt8 *>(arg) &&
			!typeid_cast<const DataTypeUInt16 *>(arg) &&
			!typeid_cast<const DataTypeUInt32 *>(arg) &
			!typeid_cast<const DataTypeUInt64 *>(arg))
		{
			throw Exception{
				"Illegal type " + arg->getName() + " of argument of function " + getName(),
				ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT
			};
		}

		return std::make_shared<DataTypeArray>(arg->clone());
	}

	template <typename T>
	bool executeInternal(Block & block, const IColumn * const arg, const size_t result)
	{
		if (const auto in = typeid_cast<const ColumnVector<T> *>(arg))
		{
			const auto & in_data = in->getData();
			const auto total_values = std::accumulate(std::begin(in_data), std::end(in_data), std::size_t{},
				[this] (const std::size_t lhs, const std::size_t rhs) {
					const auto sum = lhs + rhs;
					if (sum < lhs)
						throw Exception{
							"A call to function " + getName() + " overflows, investigate the values of arguments you are passing",
							ErrorCodes::ARGUMENT_OUT_OF_BOUND
						};

					return sum;
				});

			if (total_values > max_elements)
				throw Exception{
					"A call to function " + getName() + " would produce " + std::to_string(total_values) +
						" array elements, which is greater than the allowed maximum of " + std::to_string(max_elements),
					ErrorCodes::ARGUMENT_OUT_OF_BOUND
				};

			const auto data_col = std::make_shared<ColumnVector<T>>(total_values);
			const auto out = std::make_shared<ColumnArray>(
				data_col,
				std::make_shared<ColumnArray::ColumnOffsets_t>(in->size()));
			block.getByPosition(result).column = out;

			auto & out_data = data_col->getData();
			auto & out_offsets = out->getOffsets();

			IColumn::Offset_t offset{};
			for (const auto i : ext::range(0, in->size()))
			{
				std::copy(ext::make_range_iterator(T{}), ext::make_range_iterator(in_data[i]), &out_data[offset]);
				offset += in_data[i];
				out_offsets[i] = offset;
			}

			return true;
		}
		else if (const auto in = typeid_cast<const ColumnConst<T> *>(arg))
		{
			const auto & in_data = in->getData();
			if (in->size() > std::numeric_limits<std::size_t>::max() / in_data)
				throw Exception{
					"A call to function " + getName() + " overflows, investigate the values of arguments you are passing",
					ErrorCodes::ARGUMENT_OUT_OF_BOUND
				};

			const std::size_t total_values = in->size() * in_data;
			if (total_values > max_elements)
				throw Exception{
					"A call to function " + getName() + " would produce " + std::to_string(total_values) +
						" array elements, which is greater than the allowed maximum of " + std::to_string(max_elements),
					ErrorCodes::ARGUMENT_OUT_OF_BOUND
				};

			const auto data_col = std::make_shared<ColumnVector<T>>(total_values);
			const auto out = std::make_shared<ColumnArray>(
				data_col,
				std::make_shared<ColumnArray::ColumnOffsets_t>(in->size()));
			block.getByPosition(result).column = out;

			auto & out_data = data_col->getData();
			auto & out_offsets = out->getOffsets();

			IColumn::Offset_t offset{};
			for (const auto i : ext::range(0, in->size()))
			{
				std::copy(ext::make_range_iterator(T{}), ext::make_range_iterator(in_data), &out_data[offset]);
				offset += in_data;
				out_offsets[i] = offset;
			}

			return true;
		}

		return false;
	}

	void executeImpl(Block & block, const ColumnNumbers & arguments, const size_t result) override
	{
		const auto col = block.getByPosition(arguments[0]).column.get();

		if (!executeInternal<UInt8>(block, col, result) &&
			!executeInternal<UInt16>(block, col, result) &&
			!executeInternal<UInt32>(block, col, result) &&
			!executeInternal<UInt64>(block, col, result))
		{
			throw Exception{
				"Illegal column " + col->getName() + " of argument of function " + getName(),
				ErrorCodes::ILLEGAL_COLUMN
			};
		}
	}
};


class FunctionEmptyArrayToSingle : public IFunction
{
public:
	static constexpr auto name = "emptyArrayToSingle";
	static FunctionPtr create(const Context & context);

	/// Получить имя функции.
	String getName() const override;

	/// Получить типы результата по типам аргументов. Если функция неприменима для данных аргументов - кинуть исключение.
	DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override;

	/// Выполнить функцию над блоком.
	void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override;

private:
	bool executeConst(Block & block, const ColumnNumbers & arguments, size_t result);

	template <typename T>
	bool executeNumber(
		const IColumn & src_data, const ColumnArray::Offsets_t & src_offsets,
		IColumn & res_data_col, ColumnArray::Offsets_t & res_offsets);

	bool executeFixedString(
		const IColumn & src_data, const ColumnArray::Offsets_t & src_offsets,
		IColumn & res_data_col, ColumnArray::Offsets_t & res_offsets);

	bool executeString(
		const IColumn & src_data, const ColumnArray::Offsets_t & src_array_offsets,
		IColumn & res_data_col, ColumnArray::Offsets_t & res_array_offsets);
};


class FunctionArrayReverse : public IFunction
{
public:
	static constexpr auto name = "reverse";
	static FunctionPtr create(const Context & context);

	/// Получить имя функции.
	String getName() const override;

	/// Получить типы результата по типам аргументов. Если функция неприменима для данных аргументов - кинуть исключение.
	DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override;

	/// Выполнить функцию над блоком.
	void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override;

private:
	bool executeConst(Block & block, const ColumnNumbers & arguments, size_t result);

	template <typename T>
	bool executeNumber(
		const IColumn & src_data, const ColumnArray::Offsets_t & src_offsets,
		IColumn & res_data_col);

	bool executeFixedString(
		const IColumn & src_data, const ColumnArray::Offsets_t & src_offsets,
		IColumn & res_data_col);

	bool executeString(
		const IColumn & src_data, const ColumnArray::Offsets_t & src_array_offsets,
		IColumn & res_data_col);
};


/** Применяет к массиву агрегатную функцию и возвращает её результат.
  * Также может быть применена к нескольким массивам одинаковых размеров, если агрегатная функция принимает несколько аргументов.
  */
class FunctionArrayReduce : public IFunction
{
public:
	static constexpr auto name = "arrayReduce";
	static FunctionPtr create(const Context & context);

	/// Получить имя функции.
	String getName() const override;

	void getReturnTypeAndPrerequisitesImpl(
		const ColumnsWithTypeAndName & arguments,
		DataTypePtr & out_return_type,
		std::vector<ExpressionAction> & out_prerequisites) override;

	void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override;
private:
	AggregateFunctionPtr aggregate_function;
};


struct NameHas			{ static constexpr auto name = "has"; };
struct NameIndexOf		{ static constexpr auto name = "indexOf"; };
struct NameCountEqual	{ static constexpr auto name = "countEqual"; };

using FunctionHas = FunctionArrayIndex<IndexToOne, NameHas>;
using FunctionIndexOf = FunctionArrayIndex<IndexIdentity, NameIndexOf>;
using FunctionCountEqual = FunctionArrayIndex<IndexCount, NameCountEqual>;

using FunctionEmptyArrayUInt8 = FunctionEmptyArray<DataTypeUInt8>;
using FunctionEmptyArrayUInt16 = FunctionEmptyArray<DataTypeUInt16>;
using FunctionEmptyArrayUInt32 = FunctionEmptyArray<DataTypeUInt32>;
using FunctionEmptyArrayUInt64 = FunctionEmptyArray<DataTypeUInt64>;
using FunctionEmptyArrayInt8 = FunctionEmptyArray<DataTypeInt8>;
using FunctionEmptyArrayInt16 = FunctionEmptyArray<DataTypeInt16>;
using FunctionEmptyArrayInt32 = FunctionEmptyArray<DataTypeInt32>;
using FunctionEmptyArrayInt64 = FunctionEmptyArray<DataTypeInt64>;
using FunctionEmptyArrayFloat32 = FunctionEmptyArray<DataTypeFloat32>;
using FunctionEmptyArrayFloat64 = FunctionEmptyArray<DataTypeFloat64>;
using FunctionEmptyArrayDate = FunctionEmptyArray<DataTypeDate>;
using FunctionEmptyArrayDateTime = FunctionEmptyArray<DataTypeDateTime>;
using FunctionEmptyArrayString = FunctionEmptyArray<DataTypeString>;


}
