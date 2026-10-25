/*
 * ft.h
 *
 * A small framework for the unit tests: each test registers itself, and a
 * failed check says what differs from what, and where.
 *
 * Tests are compiled with the firmware's headers, the runner with the
 * host's, so this header uses the types of neither.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

void ft_register(const char *name, void (*fn)(void));

/* A failed check: the test goes on, and fails at its end. */
void ft_fail(const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* A failure after which the test cannot go on. */
void ft_abort(const char *file, int line, const char *fmt, ...)
    __attribute__((noreturn, format(printf, 3, 4)));

/* Return nonzero when equal, else report the first difference. */
int ft_check_mem(const char *file, int line, const char *a_expr,
                 const char *b_expr, const void *a, const void *b,
                 unsigned long len);
int ft_check_str(const char *file, int line, const char *a_expr,
                 const char *b_expr, const char *a, const char *b);

/* The firmware's console output since the test began, printed with its
 * failures. */
const char *ft_fw_log(void);
void ft_fw_log_clear(void);

#define TEST(name)                                                  \
    static void ft_test_##name(void);                               \
    static void __attribute__((constructor)) ft_reg_##name(void)    \
    {                                                               \
        ft_register(#name, ft_test_##name);                         \
    }                                                               \
    static void ft_test_##name(void)

#define CHECK(cond) do {                                            \
    if (!(cond))                                                    \
        ft_fail(__FILE__, __LINE__, "CHECK(%s) failed", #cond);     \
} while (0)

#define REQUIRE(cond) do {                                          \
    if (!(cond))                                                    \
        ft_abort(__FILE__, __LINE__, "REQUIRE(%s) failed", #cond);  \
} while (0)

#define CHECK_EQ(a, b) do {                                         \
    long long ft_a = (long long)(a), ft_b = (long long)(b);         \
    if (ft_a != ft_b)                                               \
        ft_fail(__FILE__, __LINE__,                                 \
                "%s == %s failed: %lld (0x%llx) != %lld (0x%llx)",  \
                #a, #b, ft_a, ft_a, ft_b, ft_b);                    \
} while (0)

#define CHECK_MEM(a, b, len) \
    ft_check_mem(__FILE__, __LINE__, #a, #b, a, b, len)

#define CHECK_STR(a, b) \
    ft_check_str(__FILE__, __LINE__, #a, #b, a, b)

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
