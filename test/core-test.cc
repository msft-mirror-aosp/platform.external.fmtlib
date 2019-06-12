// Formatting library for C++ - core tests
//
// Copyright (c) 2012 - present, Victor Zverovich
// All rights reserved.
//
// For the license information refer to format.h.

#include <algorithm>
#include <climits>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>
#include <memory>

#include "test-assert.h"

#include "gmock.h"

// Check if fmt/core.h compiles with windows.h included before it.
#ifdef _WIN32
# include <windows.h>
#endif

#include "fmt/core.h"

#undef min
#undef max

using fmt::basic_format_arg;
using fmt::internal::basic_buffer;
using fmt::internal::value;
using fmt::string_view;

using testing::_;
using testing::StrictMock;

namespace {

struct test_struct {};

template <typename Context, typename T>
basic_format_arg<Context> make_arg(const T &value) {
  return fmt::internal::make_arg<Context>(value);
}
}  // namespace

FMT_BEGIN_NAMESPACE
template <typename Char>
struct formatter<test_struct, Char> {
  template <typename ParseContext>
  auto parse(ParseContext &ctx) -> decltype(ctx.begin()) {
    return ctx.begin();
  }

  typedef std::back_insert_iterator<basic_buffer<Char>> iterator;

  auto format(test_struct, basic_format_context<iterator, char> &ctx)
      -> decltype(ctx.out()) {
    const Char *test = "test";
    return std::copy_n(test, std::strlen(test), ctx.out());
  }
};
FMT_END_NAMESPACE

#if !FMT_GCC_VERSION || FMT_GCC_VERSION >= 470
TEST(BufferTest, Noncopyable) {
  EXPECT_FALSE(std::is_copy_constructible<basic_buffer<char> >::value);
#if !FMT_MSC_VER
  // std::is_copy_assignable is broken in MSVC2013.
  EXPECT_FALSE(std::is_copy_assignable<basic_buffer<char> >::value);
#endif
}

TEST(BufferTest, Nonmoveable) {
  EXPECT_FALSE(std::is_move_constructible<basic_buffer<char> >::value);
#if !FMT_MSC_VER
  // std::is_move_assignable is broken in MSVC2013.
  EXPECT_FALSE(std::is_move_assignable<basic_buffer<char> >::value);
#endif
}
#endif

// A test buffer with a dummy grow method.
template <typename T>
struct test_buffer : basic_buffer<T> {
  void grow(std::size_t capacity) { this->set(FMT_NULL, capacity); }
};

template <typename T>
struct mock_buffer : basic_buffer<T> {
  MOCK_METHOD1(do_grow, void (std::size_t capacity));

  void grow(std::size_t capacity) {
    this->set(this->data(), capacity);
    do_grow(capacity);
  }

  mock_buffer() {}
  mock_buffer(T *data) { this->set(data, 0); }
  mock_buffer(T *data, std::size_t capacity) { this->set(data, capacity); }
};

TEST(BufferTest, Ctor) {
  {
    mock_buffer<int> buffer;
    EXPECT_EQ(FMT_NULL, &buffer[0]);
    EXPECT_EQ(static_cast<size_t>(0), buffer.size());
    EXPECT_EQ(static_cast<size_t>(0), buffer.capacity());
  }
  {
    int dummy;
    mock_buffer<int> buffer(&dummy);
    EXPECT_EQ(&dummy, &buffer[0]);
    EXPECT_EQ(static_cast<size_t>(0), buffer.size());
    EXPECT_EQ(static_cast<size_t>(0), buffer.capacity());
  }
  {
    int dummy;
    std::size_t capacity = std::numeric_limits<std::size_t>::max();
    mock_buffer<int> buffer(&dummy, capacity);
    EXPECT_EQ(&dummy, &buffer[0]);
    EXPECT_EQ(static_cast<size_t>(0), buffer.size());
    EXPECT_EQ(capacity, buffer.capacity());
  }
}

struct dying_buffer : test_buffer<int> {
  MOCK_METHOD0(die, void());
  ~dying_buffer() { die(); }
};

TEST(BufferTest, VirtualDtor) {
  typedef StrictMock<dying_buffer> stict_mock_buffer;
  stict_mock_buffer *mock_buffer = new stict_mock_buffer();
  EXPECT_CALL(*mock_buffer, die());
  basic_buffer<int> *buffer = mock_buffer;
  delete buffer;
}

TEST(BufferTest, Access) {
  char data[10];
  mock_buffer<char> buffer(data, sizeof(data));
  buffer[0] = 11;
  EXPECT_EQ(11, buffer[0]);
  buffer[3] = 42;
  EXPECT_EQ(42, *(&buffer[0] + 3));
  const basic_buffer<char> &const_buffer = buffer;
  EXPECT_EQ(42, const_buffer[3]);
}

TEST(BufferTest, Resize) {
  char data[123];
  mock_buffer<char> buffer(data, sizeof(data));
  buffer[10] = 42;
  EXPECT_EQ(42, buffer[10]);
  buffer.resize(20);
  EXPECT_EQ(20u, buffer.size());
  EXPECT_EQ(123u, buffer.capacity());
  EXPECT_EQ(42, buffer[10]);
  buffer.resize(5);
  EXPECT_EQ(5u, buffer.size());
  EXPECT_EQ(123u, buffer.capacity());
  EXPECT_EQ(42, buffer[10]);
  // Check if resize calls grow.
  EXPECT_CALL(buffer, do_grow(124));
  buffer.resize(124);
  EXPECT_CALL(buffer, do_grow(200));
  buffer.resize(200);
}

TEST(BufferTest, Clear) {
  test_buffer<char> buffer;
  buffer.resize(20);
  buffer.resize(0);
  EXPECT_EQ(static_cast<size_t>(0), buffer.size());
  EXPECT_EQ(20u, buffer.capacity());
}

TEST(BufferTest, Append) {
  char data[15];
  mock_buffer<char> buffer(data, 10);
  const char *test = "test";
  buffer.append(test, test + 5);
  EXPECT_STREQ(test, &buffer[0]);
  EXPECT_EQ(5u, buffer.size());
  buffer.resize(10);
  EXPECT_CALL(buffer, do_grow(12));
  buffer.append(test, test + 2);
  EXPECT_EQ('t', buffer[10]);
  EXPECT_EQ('e', buffer[11]);
  EXPECT_EQ(12u, buffer.size());
}

TEST(BufferTest, AppendAllocatesEnoughStorage) {
  char data[19];
  mock_buffer<char> buffer(data, 10);
  const char *test = "abcdefgh";
  buffer.resize(10);
  EXPECT_CALL(buffer, do_grow(19));
  buffer.append(test, test + 9);
}

TEST(ArgTest, FormatArgs) {
  fmt::format_args args;
  EXPECT_FALSE(args.get(1));
}

struct custom_context {
  typedef char char_type;

  template <typename T>
  struct formatter_type {
    struct type {
      template <typename ParseContext>
      auto parse(ParseContext &ctx) -> decltype(ctx.begin()) {
        return ctx.begin();
      }

      const char *format(const T &, custom_context& ctx) {
        ctx.called = true;
        return FMT_NULL;
      }
    };
  };

  bool called;

  fmt::format_parse_context parse_context() {
    return fmt::format_parse_context("");
  }
  void advance_to(const char *) {}
};

TEST(ArgTest, MakeValueWithCustomContext) {
  test_struct t;
  fmt::internal::value<custom_context> arg =
      fmt::internal::make_value<custom_context>(t);
  custom_context ctx = {false};
  arg.custom.format(&t, ctx);
  EXPECT_TRUE(ctx.called);
}

FMT_BEGIN_NAMESPACE
namespace internal {
template <typename Char>
bool operator==(custom_value<Char> lhs, custom_value<Char> rhs) {
  return lhs.value == rhs.value;
}
}
FMT_END_NAMESPACE

// Use a unique result type to make sure that there are no undesirable
// conversions.
struct test_result {};

template <typename T>
struct mock_visitor {
  template <typename U>
  struct result { typedef test_result type; };

  mock_visitor() {
    ON_CALL(*this, visit(_)).WillByDefault(testing::Return(test_result()));
  }

  MOCK_METHOD1_T(visit, test_result (T value));
  MOCK_METHOD0_T(unexpected, void ());

  test_result operator()(T value) { return visit(value); }

  template <typename U>
  test_result operator()(U) {
    unexpected();
    return test_result();
  }
};

template <typename T>
struct visit_type { typedef T Type; };

#define VISIT_TYPE(Type_, visit_type_) \
  template <> \
  struct visit_type<Type_> { typedef visit_type_ Type; }

VISIT_TYPE(signed char, int);
VISIT_TYPE(unsigned char, unsigned);
VISIT_TYPE(short, int);
VISIT_TYPE(unsigned short, unsigned);

#if LONG_MAX == INT_MAX
VISIT_TYPE(long, int);
VISIT_TYPE(unsigned long, unsigned);
#else
VISIT_TYPE(long, long long);
VISIT_TYPE(unsigned long, unsigned long long);
#endif

VISIT_TYPE(float, double);

#define CHECK_ARG_(Char, expected, value) { \
  testing::StrictMock<mock_visitor<decltype(expected)>> visitor; \
  EXPECT_CALL(visitor, visit(expected)); \
  typedef std::back_insert_iterator<basic_buffer<Char>> iterator; \
  fmt::visit(visitor, \
      make_arg<fmt::basic_format_context<iterator, Char>>(value)); \
}

#define CHECK_ARG(value, typename_) { \
  typedef decltype(value) value_type; \
  typename_ visit_type<value_type>::Type expected = value; \
  CHECK_ARG_(char, expected, value) \
  CHECK_ARG_(wchar_t, expected, value) \
}

template <typename T>
class NumericArgTest : public testing::Test {};

typedef ::testing::Types<
  bool, signed char, unsigned char, signed, unsigned short,
  int, unsigned, long, unsigned long, long long, unsigned long long,
  float, double, long double> Types;
TYPED_TEST_CASE(NumericArgTest, Types);

template <typename T>
typename std::enable_if<std::is_integral<T>::value, T>::type test_value() {
  return static_cast<T>(42);
}

template <typename T>
typename std::enable_if<std::is_floating_point<T>::value, T>::type
    test_value() {
  return static_cast<T>(4.2);
}

TYPED_TEST(NumericArgTest, MakeAndVisit) {
  CHECK_ARG(test_value<TypeParam>(), typename);
  CHECK_ARG(std::numeric_limits<TypeParam>::min(), typename);
  CHECK_ARG(std::numeric_limits<TypeParam>::max(), typename);
}

TEST(ArgTest, CharArg) {
  CHECK_ARG_(char, 'a', 'a');
  CHECK_ARG_(wchar_t, L'a', 'a');
  CHECK_ARG_(wchar_t, L'a', L'a');
}

TEST(ArgTest, StringArg) {
  char str_data[] = "test";
  char *str = str_data;
  const char *cstr = str;
  CHECK_ARG_(char, cstr, str);

  string_view sref(str);
  CHECK_ARG_(char, sref, std::string(str));
}

TEST(ArgTest, WStringArg) {
  wchar_t str_data[] = L"test";
  wchar_t *str = str_data;
  const wchar_t *cstr = str;

  fmt::wstring_view sref(str);
  CHECK_ARG_(wchar_t, cstr, str);
  CHECK_ARG_(wchar_t, cstr, cstr);
  CHECK_ARG_(wchar_t, sref, std::wstring(str));
  CHECK_ARG_(wchar_t, sref, fmt::wstring_view(str));
}

TEST(ArgTest, PointerArg) {
  void *p = FMT_NULL;
  const void *cp = FMT_NULL;
  CHECK_ARG_(char, cp, p);
  CHECK_ARG_(wchar_t, cp, p);
  CHECK_ARG(cp, );
}

struct check_custom {
  test_result operator()(
      fmt::basic_format_arg<fmt::format_context>::handle h) const {
    struct test_buffer : fmt::internal::basic_buffer<char> {
      char data[10];
      test_buffer() : fmt::internal::basic_buffer<char>(data, 0, 10) {}
      void grow(std::size_t) {}
    } buffer;
    fmt::internal::basic_buffer<char> &base = buffer;
    fmt::format_context ctx(std::back_inserter(base), "", fmt::format_args());
    h.format(ctx);
    EXPECT_EQ("test", std::string(buffer.data, buffer.size()));
    return test_result();
  }
};

TEST(ArgTest, CustomArg) {
  test_struct test;
  typedef mock_visitor<fmt::basic_format_arg<fmt::format_context>::handle>
    visitor;
  testing::StrictMock<visitor> v;
  EXPECT_CALL(v, visit(_)).WillOnce(testing::Invoke(check_custom()));
  fmt::visit(v, make_arg<fmt::format_context>(test));
}

TEST(ArgTest, VisitInvalidArg) {
  testing::StrictMock< mock_visitor<fmt::monostate> > visitor;
  EXPECT_CALL(visitor, visit(_));
  fmt::basic_format_arg<fmt::format_context> arg;
  visit(visitor, arg);
}

TEST(StringViewTest, Length) {
  // Test that StringRef::size() returns string length, not buffer size.
  char str[100] = "some string";
  EXPECT_EQ(std::strlen(str), string_view(str).size());
  EXPECT_LT(std::strlen(str), sizeof(str));
}

// Check string_view's comparison operator.
template <template <typename> class Op>
void check_op() {
  const char *inputs[] = {"foo", "fop", "fo"};
  std::size_t num_inputs = sizeof(inputs) / sizeof(*inputs);
  for (std::size_t i = 0; i < num_inputs; ++i) {
    for (std::size_t j = 0; j < num_inputs; ++j) {
      string_view lhs(inputs[i]), rhs(inputs[j]);
      EXPECT_EQ(Op<int>()(lhs.compare(rhs), 0), Op<string_view>()(lhs, rhs));
    }
  }
}

TEST(StringViewTest, Compare) {
  EXPECT_EQ(string_view("foo").compare(string_view("foo")), 0);
  EXPECT_GT(string_view("fop").compare(string_view("foo")), 0);
  EXPECT_LT(string_view("foo").compare(string_view("fop")), 0);
  EXPECT_GT(string_view("foo").compare(string_view("fo")), 0);
  EXPECT_LT(string_view("fo").compare(string_view("foo")), 0);
  check_op<std::equal_to>();
  check_op<std::not_equal_to>();
  check_op<std::less>();
  check_op<std::less_equal>();
  check_op<std::greater>();
  check_op<std::greater_equal>();
}

enum basic_enum {};

TEST(CoreTest, ConvertToInt) {
  EXPECT_FALSE((fmt::convert_to_int<char, char>::value));
  EXPECT_FALSE((fmt::convert_to_int<const char *, char>::value));
  EXPECT_TRUE((fmt::convert_to_int<basic_enum, char>::value));
}

enum enum_with_underlying_type : char {};

TEST(CoreTest, IsEnumConvertibleToInt) {
  EXPECT_TRUE((fmt::convert_to_int<enum_with_underlying_type, char>::value));
}

namespace my_ns {
template <typename Char>
class my_string {
 public:
  my_string(const Char *s) : s_(s) {}
  const Char * data() const FMT_NOEXCEPT { return s_.data(); }
  std::size_t length() const FMT_NOEXCEPT { return s_.size(); }
  operator const Char*() const { return s_.c_str(); }
 private:
  std::basic_string<Char> s_;
};

template <typename Char>
inline fmt::basic_string_view<Char>
    to_string_view(const my_string<Char> &s) FMT_NOEXCEPT {
  return { s.data(), s.length() };
}

struct non_string {};
}

namespace FakeQt {
class QString {
 public:
  QString(const wchar_t *s) : s_(std::make_shared<std::wstring>(s)) {}
  const wchar_t *utf16() const FMT_NOEXCEPT { return s_->data(); }
  int size() const FMT_NOEXCEPT { return static_cast<int>(s_->size()); }
#ifdef FMT_STRING_VIEW
  operator FMT_STRING_VIEW<wchar_t>() const FMT_NOEXCEPT { return *s_; }
#endif
 private:
  std::shared_ptr<std::wstring> s_;
};

inline fmt::basic_string_view<wchar_t> to_string_view(
    const QString &s) FMT_NOEXCEPT {
  return {s.utf16(),
          static_cast<std::size_t>(s.size())};
}
}

template <typename T>
class IsStringTest : public testing::Test {};

typedef ::testing::Types<char, wchar_t, char16_t, char32_t> StringCharTypes;
TYPED_TEST_CASE(IsStringTest, StringCharTypes);

namespace {
template <typename Char>
struct derived_from_string_view : fmt::basic_string_view<Char> {};
}

TYPED_TEST(IsStringTest, IsString) {
  EXPECT_TRUE((fmt::internal::is_string<TypeParam *>::value));
  EXPECT_TRUE((fmt::internal::is_string<const TypeParam *>::value));
  EXPECT_TRUE((fmt::internal::is_string<TypeParam[2]>::value));
  EXPECT_TRUE((fmt::internal::is_string<const TypeParam[2]>::value));
  EXPECT_TRUE((fmt::internal::is_string<std::basic_string<TypeParam>>::value));
  EXPECT_TRUE(
        (fmt::internal::is_string<fmt::basic_string_view<TypeParam>>::value));
  EXPECT_TRUE(
        (fmt::internal::is_string<derived_from_string_view<TypeParam>>::value));
#ifdef FMT_STRING_VIEW
  EXPECT_TRUE((fmt::internal::is_string<FMT_STRING_VIEW<TypeParam>>::value));
#endif
  EXPECT_TRUE((fmt::internal::is_string<my_ns::my_string<TypeParam>>::value));
  EXPECT_FALSE((fmt::internal::is_string<my_ns::non_string>::value));
  EXPECT_TRUE((fmt::internal::is_string<FakeQt::QString>::value));
}

TEST(CoreTest, Format) {
  // This should work without including fmt/format.h.
#ifdef FMT_FORMAT_H_
# error fmt/format.h must not be included in the core test
#endif
  EXPECT_EQ(fmt::format("{}", 42), "42");
}

TEST(CoreTest, FormatTo) {
  // This should work without including fmt/format.h.
#ifdef FMT_FORMAT_H_
# error fmt/format.h must not be included in the core test
#endif
  std::string s;
  fmt::format_to(std::back_inserter(s), "{}", 42);
  EXPECT_EQ(s, "42");
}

TEST(CoreTest, ToStringViewForeignStrings) {
  using namespace my_ns;
  using namespace FakeQt;
  EXPECT_EQ(to_string_view(my_string<char>("42")), "42");
  EXPECT_EQ(to_string_view(my_string<wchar_t>(L"42")), L"42");
  EXPECT_EQ(to_string_view(QString(L"42")), L"42");
  fmt::internal::type type =
      fmt::internal::get_type<fmt::format_context, my_string<char>>::value;
  EXPECT_EQ(type, fmt::internal::string_type);
  type =
      fmt::internal::get_type<fmt::wformat_context, my_string<wchar_t>>::value;
  EXPECT_EQ(type, fmt::internal::string_type);
  type = fmt::internal::get_type<fmt::wformat_context, QString>::value;
  EXPECT_EQ(type, fmt::internal::string_type);
  // Does not compile: only wide format contexts are compatible with QString!
  // type = fmt::internal::get_type<fmt::format_context, QString>::value;
}

TEST(CoreTest, FormatForeignStrings) {
  using namespace my_ns;
  using namespace FakeQt;
  EXPECT_EQ(fmt::format(my_string<char>("{}"), 42), "42");
  EXPECT_EQ(fmt::format(my_string<wchar_t>(L"{}"), 42), L"42");
  EXPECT_EQ(fmt::format(QString(L"{}"), 42), L"42");
  EXPECT_EQ(fmt::format(QString(L"{}"), my_string<wchar_t>(L"42")), L"42");
  EXPECT_EQ(fmt::format(my_string<wchar_t>(L"{}"), QString(L"42")), L"42");
}

struct implicitly_convertible_to_string_view {
  operator fmt::string_view() const { return "foo"; }
};

TEST(FormatterTest, FormatImplicitlyConvertibleToStringView) {
  EXPECT_EQ("foo", fmt::format("{}", implicitly_convertible_to_string_view()));
}

// std::is_constructible is broken in MSVC until version 2015.
#if FMT_USE_EXPLICIT && (!FMT_MSC_VER || FMT_MSC_VER >= 1900)
struct explicitly_convertible_to_string_view {
  explicit operator fmt::string_view() const { return "foo"; }
};

TEST(FormatterTest, FormatExplicitlyConvertibleToStringView) {
  EXPECT_EQ("foo", fmt::format("{}", explicitly_convertible_to_string_view()));
}

struct explicitly_convertible_to_wstring_view {
  explicit operator fmt::wstring_view() const { return L"foo"; }
};

TEST(FormatterTest, FormatExplicitlyConvertibleToWStringView) {
  EXPECT_EQ(L"foo",
            fmt::format(L"{}", explicitly_convertible_to_wstring_view()));
}

struct explicitly_convertible_to_string_like {
  template <
      typename String,
      typename = typename std::enable_if<
        std::is_constructible<String, const char*, std::size_t>::value>::type>
  FMT_EXPLICIT operator String() const { return String("foo", 3u); }
};

TEST(FormatterTest, FormatExplicitlyConvertibleToStringLike) {
  EXPECT_EQ("foo", fmt::format("{}", explicitly_convertible_to_string_like()));
}
#endif
