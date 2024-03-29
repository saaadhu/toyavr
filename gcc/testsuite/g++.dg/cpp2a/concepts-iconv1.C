// PR c++/67240
// { dg-do compile { target c++2a } }

template <class T, class U> concept Same = __is_same_as(T,U);

int foo(int x)
{
    return x;
}
 
template <typename T>
concept C1 = requires (T x) {
    {foo(x)} -> Same<int&>;
};

template <typename T>
concept C2 = requires (T x) {
    {foo(x)} -> Same<void>;
};
 
static_assert( C1<int> );	// { dg-error "assert" }
static_assert( C2<int> );	// { dg-error "assert" }
