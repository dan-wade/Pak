//Copyright (C) 2016 Daniel Wade < danwade@hotmail.com >
//
//This source code is subject to the terms of the MIT license
//See http://opensource.org/licenses/MIT

#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <utility>
#include <cstring> //memcpy

//Can be commented out if portability isn't needed
#define PAK_PORTABLE_BINARY

#ifdef PAK_PORTABLE_BINARY

	template<typename T>
	typename std::enable_if<(sizeof(T) > 1) && std::is_integral<T>::value, bool>::type
		shouldByteSwap()
	{
		int endianTest = 1;
		bool isBigEndian = (*(std::uint8_t*)&endianTest == 0);
		return isBigEndian; 
	}

	template<typename T>
	typename std::enable_if<(sizeof(T) == 1) || !std::is_integral<T>::value, bool>::type
		shouldByteSwap()
	{
		return false;
	}

	template<typename T, std::size_t Size>
	using enable_integral =
		typename std::enable_if<std::is_integral<T>::value && (sizeof(T) == Size)>::type;

	#if defined(_MSC_VER)
		#include <cstdlib>
		template<typename T> enable_integral<T, 2> byteSwap(T& t) { t = _byteswap_ushort(t); }
		template<typename T> enable_integral<T, 4> byteSwap(T& t) { t = _byteswap_ulong(t); }
		template<typename T> enable_integral<T, 8> byteSwap(T& t) { t = _byteswap_uint64(t); }
	#elif defined(__GNUC__) || defined(__clang__)
		template<typename T> enable_integral<T, 2> byteSwap(T& t) { t = __builtin_bswap16(t); }
		template<typename T> enable_integral<T, 4> byteSwap(T& t) { t = __builtin_bswap32(t); }
		template<typename T> enable_integral<T, 8> byteSwap(T& t) { t = __builtin_bswap64(t); }
	#else
		template<typename T>
		void byteSwap(T& t)
		{
			auto* begin = (std::uint8_t*)(&t);
			auto* end = (std::uint8_t*)(&t + 1) - 1;

			while(begin < end)
				std::swap(*begin++, *end--);
		}
	#endif

	void byteSwap(...) { }

#endif

/*	This macro allows for concise expression SFINAE trait definitions
	
	Each trait can be used to test a given type against a list of expressions. The trait's value
	will be true if the expressions compile for the given type, and false if they don't.
*/
#define DEFINE_TRAIT(NAME, ...)																	\
template<typename Type, typename Op = void>														\
struct NAME																						\
{																								\
	template<typename T>	static auto test(T* t) -> decltype(__VA_ARGS__, std::true_type());	\
	template<typename>		static auto test(...) -> std::false_type;							\
	static const bool value = std::is_same<std::true_type, decltype(test<Type>(0))>::value;		\
};

DEFINE_TRAIT(is_userdefined_internal, t->serialize(std::declval<Op>()))
DEFINE_TRAIT(is_userdefined_external, serialize(std::declval<Op>(), *t))
DEFINE_TRAIT(is_iterator, ++(*t), *(*t)) //Includes pointers
DEFINE_TRAIT(is_container, t->begin() != t->end(), t->get_allocator()) //Excludes std::array
DEFINE_TRAIT(is_map, t->emplace_hint(t->end()))
#undef DEFINE_TRAIT

class Pak
{
public:
	Pak() {}

	template<typename... Args>
	Pak(Args&&... args)
	{
		write(args...);
	}

	template<typename... Args>
	void write(Args&&... args)
	{
		parse(Op<Reserve>{*this}, args...);
		bytes.resize(bytes.size() + reserveSize);
		parse(Op<Write>{*this}, args...);
	}

	template<typename... Args>
	void read(Args&&... args)
	{
		parse(Op<Read>{*this}, args...);
	}

	template<typename T>
	T read()
	{
		T t;
		parse(Op<Read>{*this}, t);
		return t;
	}

	const std::uint8_t* data() const
	{
		return bytes.data();
	}

	std::size_t size() const
	{
		return bytes.size();
	}

	template<typename T>
	struct Op
	{
		Pak& pak;

		template<typename... Args>
		void operator()(Args&&... args)
		{
			pak.parse(*this, args...);
		}
	};

	struct Reserve;
	struct Write;
	struct Read;

private:
	// Variadic Template Evaluation ///////////////////////////////////////////////////////////////
	template<typename OpType, typename T, typename... Args>
	typename std::enable_if<is_iterator<typename std::remove_reference<T>::type>::value>::type
		parse(Op<OpType> op, T&& begin, T&& end, Args&&... args) //Iterator pair
	{
		//An optimized version of this is called instead when dealing with contiguous primitives
		for(auto it = begin; it != end; it++)
			parse(op, *it);

		parse(op, args...);
	}

	template<typename OpType, typename T, typename... Args>
	void parse(Op<OpType> op, T&& t, Args&&... args) //Single argument
	{
		parse(op, t);
		parse(op, args...);
	}

	template<typename OpType>
	void parse(Op<OpType>) { } //No argument

	// Contiguous Range Optimization //////////////////////////////////////////////////////////////
	template<typename T>
	using value_type = typename std::iterator_traits<T>::value_type;

	template<typename T>
	using enable_if_contiguous_range = typename std::enable_if<
		is_iterator<T>::value &&
		std::is_arithmetic<value_type<T>>::value &&
		std::is_same<	typename std::iterator_traits<T>::iterator_category,
						std::random_access_iterator_tag>::value>::type;

	template<typename T, typename... Args>
	enable_if_contiguous_range<T> parse(Op<Reserve> op, T&& begin, T&& end, Args&&... args)
	{
		reserveSize += (end - begin) * sizeof(*begin);
		parse(op, args...);
	}

	template<typename T>
	void copyPrimitiveArray(T* dst, T* src, std::size_t numBytes)
	{
		if(numBytes > 0)
		{
			std::memcpy(dst, src, numBytes);

			#ifdef PAK_PORTABLE_BINARY
			if(shouldByteSwap<T>())
			{
				auto* current = dst;
				auto* end = (T*)((std::uint8_t*)dst + numBytes);

				while(current < end)
					byteSwap(*current++);
			}
			#endif
		}
	}

	template<typename T, typename... Args>
	enable_if_contiguous_range<T> parse(Op<Write> op, T&& begin, T&& end, Args&&... args)
	{
		std::size_t numBytes = (end - begin) * sizeof(*begin);
		copyPrimitiveArray((value_type<T>*)&bytes[writePosition], &(*begin), numBytes);
		writePosition += numBytes;

		parse(op, args...);
	}

	template<typename T, typename... Args>
	enable_if_contiguous_range<T> parse(Op<Read> op, T&& begin, T&& end, Args&&... args)
	{
		std::size_t numBytes = (end - begin) * sizeof(*begin);
		copyPrimitiveArray(&(*begin), (value_type<T>*)&bytes[readPosition], numBytes);
		readPosition += numBytes;
		
		parse(op, args...);
	}

	// Static Array ///////////////////////////////////////////////////////////////////////////////
	template<typename OpType, typename T, std::size_t S>
	void parse(Op<OpType> op, T(&t)[S])
	{
		typedef typename std::remove_all_extents<T>::type Element;
		auto* begin = (Element*)t;
		auto* end = (Element*)(&t + 1);

		parse(op, begin, end);
	}

	// Pair ///////////////////////////////////////////////////////////////////////////////////////
	template<typename OpType, typename T, typename U>
	void parse(Op<OpType> op, std::pair<T, U>& t)
	{
		parse(op, t.first, t.second);
	}

	// Array //////////////////////////////////////////////////////////////////////////////////////
	template<typename OpType, typename T, std::size_t S>
	void parse(Op<OpType> op, std::array<T, S>& t)
	{
		parse(op, t.begin(), t.end());
	}

	// Container //////////////////////////////////////////////////////////////////////////////////
	template<typename OpType, typename T>
	typename std::enable_if<is_container<T>::value>::type
		parse(Op<OpType> op, T& container)
	{
		auto containerSize = static_cast<std::uint32_t>(container.size());
		parse(op, containerSize);
		parse(op, container.begin(), container.end());
	}

	template<typename T>
	typename std::enable_if<is_map<T>::value>::type
		parse(Op<Read>, T& t)
	{
		auto numElements = read<std::uint32_t>();

		typedef std::pair<	typename std::remove_const<typename T::key_type>::type,
							typename T::mapped_type> value_type;

		t.clear();
		for(std::uint32_t i = 0; i < numElements; i++)
			t.insert(t.end(), read<value_type>());
	}

	template<typename T>
	typename std::enable_if<is_container<T>::value && !is_map<T>::value>::type
		parse(Op<Read> op, T& t)
	{
		t.resize(read<std::uint32_t>());
		parse(op, t.begin(), t.end());
	}

	// User-Defined ///////////////////////////////////////////////////////////////////////////////
	template<typename OpType, typename T>
	typename std::enable_if<is_userdefined_internal<T, Op<OpType>>::value>::type
		parse(Op<OpType> op, const T& t)
	{
		static_assert(!std::is_same<OpType, Read>::value, "Pak cannot read into a const value");
		const_cast<T&>(t).serialize(op);
	}

	template<typename OpType, typename T>
	typename std::enable_if<is_userdefined_internal<T, Op<OpType>>::value>::type
		parse(Op<OpType> op, T& t)
	{
		t.serialize(op);
	}
	
	template<typename OpType, typename T>
	typename std::enable_if<is_userdefined_external<T, Op<OpType>>::value>::type
		parse(Op<OpType> op, const T& t)
	{
		static_assert(!std::is_same<OpType, Read>::value, "Pak cannot read into a const value");
		serialize(op, const_cast<T&>(t));
	}

	template<typename OpType, typename T>
	typename std::enable_if<is_userdefined_external<T, Op<OpType>>::value>::type
		parse(Op<OpType> op, T& t)
	{
		serialize(op, t);
	}

	// Primitive //////////////////////////////////////////////////////////////////////////////////
	template<typename T>
	typename std::enable_if<std::is_arithmetic<T>::value>::type
		parse(Op<Reserve>, T)
	{
		reserveSize += sizeof(T);
	}

	template<typename T>
	void copyPrimitive(T& dst, T src)
	{
		dst = src;

		#ifdef PAK_PORTABLE_BINARY
		if(shouldByteSwap<T>())
		{
			byteSwap(dst);
		}
		#endif
	}

	template<typename T>
	typename std::enable_if<std::is_arithmetic<T>::value>::type
		parse(Op<Write>, T t)
	{
		copyPrimitive(*reinterpret_cast<T*>(&bytes[writePosition]), t);
		writePosition += sizeof(T);
	}

	template<typename T>
	typename std::enable_if<std::is_arithmetic<T>::value>::type
		parse(Op<Read>, T& t)
	{
		static_assert(!std::is_const<T>::value, "Pak cannot read into a const value");
		copyPrimitive(t, *reinterpret_cast<T*>(&bytes[readPosition]));
		readPosition += sizeof(T);
	}

	std::vector<std::uint8_t> bytes;
	std::size_t reserveSize = 0;
	std::size_t writePosition = 0;
	std::size_t readPosition = 0;
};
