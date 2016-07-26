# Pak - A Lightweight C++11 Binary Serializer

What makes Pak different from other serializers?

- Pak produces a portable and space-efficient binary format suitable for networking
- Pak is a single header file
- Pak has a minimal number of dependencies
- Pak is exceptionally fast and easy to use

### Performance

Pak is generally 2-4x faster than boost::serialization's binary archive, and more space-efficient.

Pak's binary format is portable by default and will automatically handle endian conversions. This
can be toggled off by commenting out the PORTABLE_BINARY macro definition.

In almost all cases, portability has a negligible performance cost. The only exception to
this is when dealing with arrays of integers (or multi-byte characters) on big endian systems.
Such arrays have an additional

	~25% performance cost if intrinsics are available (VS, GCC, and Clang)
	~250% performance cost if intrinsics are not available

### Example
Pak can take a bunch of variables...

		float f = 0.2f;
		std::string s("gnarly");
		std::map<std::string, std::list< std::wstring>> nested;
		int staticArray[2][3][4][5];

		class FubarManager
		{
			float balance;
			std::string name;
			std::unordered_multimap<int, std::deque<double> > fubarMap;

		public:
			template<typename T>
			void serialize(T t)
			{
				t(balance, name, fubarMap);
			}

		} manager;

...and serialize them in one line using a variadic constructor

		Pak p(f, s, nested, staticArray, manager);

At which point you can save it to disk or send it over the interwebs

		someSocket.send(p.data(), p.size());

Deserializing would like...

		someSocket.receive(someBuffer, numBytesReceived);
		Pak p(someBuffer, someBuffer + numBytesReceived); 

		p.read(i, f, s, nested, manager);

### Supported Types
Pak can handle primitives, arrays, STL containers, and pairs with no extra effort. Any other type requires a serialize function.

Pak does not yet handle smart pointers or polymorphic types.

No extra meta-data is serialized, except when dealing with STL containers. Containers will have 4-bytes prepended to denote number of elements contained.

Containers can also be written via iterators. Note that containers serialized in this way will not have their size automatically prepended.

		std::vector<int> v(100);

		p.write(v);						//writes 404 bytes
		p.read(v);						//reads 404 bytes

		p.write(v.begin(), v.end());	//writes 400 bytes
		p.read(v.begin(), v.end());		//reads 400 bytes

Iterators can be passed in through std::pair as well

		p.write(std::make_pair(v.rbegin(), v.rend())); //writes 400 bytes
	
Dynamic arrays are serialized as iterator pairs.

	Serializing:
		std::uint16_t size = 300;
		float* out = new float[size];
		Pak p(size, out, out + size);

	Deserializing:
		std::uint16_t size = p.read<std::uint16_t>();
		float* in = new float[size];
		p.read(in, in + size);

User-defined classes with a serialize function are supported:

		struct Fubar
		{
			int a, b, c;

			template<typename T>
			void serialize(T t)
			{
				t(a, b, c);
			}
		};

This can also be done with a non-member function:

		template<typename T>
		void serialize(T t, Fubar& f)
		{
			t(f.a, f.b, f.c);
		}

Versioning would look like this:

		template<typename T>
		void serialize(T t)
		{
			std::uint16_t version = 2;
			t(version);

			switch(version)
			{
				case 0: t(a, c, e, f); break;
				case 1: t(a, b, c, f); break;
				case 2: t(a, b, c, d, e, f); break;
			}
		}

Split Save/Load can be done using the non-template serialize functions. This is a tad verbose as Pak requires a Reserve overload.

		void serialize(Pak::Op<Pak::Reserve> t) //"Reserve"
		{
			t(a, b, c);
		}

		void serialize(Pak::Op<Pak::Write> t) //"Save"
		{
			t(a, b, c);
		}

		void serialize(Pak::Op<Pak::Read> t) //"Load"
		{
			t(a, b, c);
		}

