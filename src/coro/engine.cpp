#include "coro/engine.hpp"

namespace coro
{

std::atomic<uint32_t> engine::m_engine_id_counter{0};

} // namespace coro
