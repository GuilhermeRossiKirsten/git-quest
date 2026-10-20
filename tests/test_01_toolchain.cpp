// Confere o ambiente: C++20, g++ >= 14 e o próprio contador de threads do avaliador.
#include <stop_token>
#include <thread>
#include <version>

#include "check.h"

TEST(toolchain_cpp20, "compilado em C++20 com g++ >= 14 (std::jthread e std::stop_token disponíveis)") {
    CHECK(__cplusplus == 202002L, "esperava __cplusplus == 202002L (C++20), obtive " << __cplusplus);
#if defined(__GNUC__) && !defined(__clang__)
    CHECK(__GNUC__ >= 14, "esperava g++ >= 14, obtive " << __GNUC__);
#endif
#if !defined(__cpp_lib_jthread)
    CHECK(false, "a biblioteca padrão não oferece std::jthread (__cpp_lib_jthread)");
#endif
    const std::size_t before = gq::thread_count();
    {
        std::jthread probe([](std::stop_token st) {
            while (!st.stop_requested()) {
                std::this_thread::yield();
            }
        });
        CHECK(gq::thread_count() == before + 1, "o contador de threads do avaliador não enxergou a nova thread");
    }
    CHECK_NO_THREADS_LEFT(before, "depois de destruir um std::jthread");
}
