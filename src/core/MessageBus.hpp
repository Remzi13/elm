#pragma once

#include "core/Std.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <typeindex>

namespace elm {

class MessageBus {
private:
    struct IProvider {
        virtual ~IProvider() = default;
        virtual uint64_t GetId() const noexcept = 0;
    };

    template <typename Query>
    struct Provider final : IProvider {
        Provider(uint64_t providerId, std::function<Query()> queryHandler)
            : id(providerId)
            , handler(std::move(queryHandler))
        {
        }

        uint64_t GetId() const noexcept override { return id; }

        uint64_t id;
        std::function<Query()> handler;
    };

public:
    class Subscription {
    public:
        Subscription() = default;
        ~Subscription() { Reset(); }

        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;

        Subscription(Subscription&& other) noexcept
            : m_bus(std::exchange(other.m_bus, nullptr))
            , m_queryType(other.m_queryType)
            , m_id(other.m_id)
        {
        }

        Subscription& operator=(Subscription&& other) noexcept
        {
            if (this != &other) {
                Reset();
                m_bus = std::exchange(other.m_bus, nullptr);
                m_queryType = other.m_queryType;
                m_id = other.m_id;
            }
            return *this;
        }

    private:
        friend class MessageBus;

        Subscription(MessageBus& bus, std::type_index queryType, uint64_t id)
            : m_bus(&bus)
            , m_queryType(queryType)
            , m_id(id)
        {
        }

        void Reset()
        {
            if (m_bus) {
                m_bus->Unregister(m_queryType, m_id);
                m_bus = nullptr;
            }
        }

        MessageBus* m_bus { nullptr };
        std::type_index m_queryType { typeid(void) };
        uint64_t m_id { 0 };
    };

    template <typename Query, typename Handler>
    [[nodiscard]] Subscription RegisterQuery(Handler&& handler)
    {
        const auto queryType = std::type_index(typeid(Query));
        if (m_providers.contains(queryType)) {
            throw std::logic_error("A window query already has a provider");
        }

        const uint64_t id = ++m_nextId;
        auto provider = MakeUnique<Provider<Query>>(
            id, std::function<Query()>(std::forward<Handler>(handler)));
        m_providers.emplace(queryType, std::move(provider));
        return Subscription(*this, queryType, id);
    }

    template <typename Query>
    [[nodiscard]] std::optional<Query> Request() const
    {
        const auto it = m_providers.find(std::type_index(typeid(Query)));
        if (it == m_providers.end()) {
            return std::nullopt;
        }

        const auto& provider = static_cast<const Provider<Query>&>(*it->second);
        return provider.handler();
    }

private:
    void Unregister(std::type_index queryType, uint64_t id)
    {
        const auto it = m_providers.find(queryType);
        if (it != m_providers.end() && it->second->GetId() == id) {
            m_providers.erase(it);
        }
    }

    UnorderedMap<std::type_index, UniquePtr<IProvider>> m_providers;
    uint64_t m_nextId { 0 };
};

} // namespace elm
