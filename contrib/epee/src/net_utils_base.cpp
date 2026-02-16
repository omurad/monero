
#include "net/net_utils_base.h"

#include <boost/uuid/uuid_io.hpp>

#include "string_tools.h"
#include "net/local_ip.h"

static inline uint32_t make_address_v4_from_v6(const boost::asio::ip::address_v6& a)
{
  const auto &bytes = a.to_bytes();
  uint32_t v4 = 0;
  v4 = (v4 << 8) | bytes[12];
  v4 = (v4 << 8) | bytes[13];
  v4 = (v4 << 8) | bytes[14];
  v4 = (v4 << 8) | bytes[15];
  return htonl(v4);
}

namespace epee { namespace net_utils
{
	bool ipv4_network_address::equal(const ipv4_network_address& other) const noexcept
	{ return is_same_host(other) && port() == other.port(); }

	bool ipv4_network_address::less(const ipv4_network_address& other) const noexcept
	{ return is_same_host(other) ? port() < other.port() : ip() < other.ip(); }

	std::string ipv4_network_address::str() const
	{ return string_tools::get_ip_string_from_int32(ip()) + ":" + std::to_string(port()); }

	std::string ipv4_network_address::host_str() const { return string_tools::get_ip_string_from_int32(ip()); }
	bool ipv4_network_address::is_loopback() const { return net_utils::is_ip_loopback(ip()); }
	bool ipv4_network_address::is_local() const { return net_utils::is_ip_local(ip()); }

	bool ipv6_network_address::equal(const ipv6_network_address& other) const noexcept
	{ return is_same_host(other) && port() == other.port(); }

	bool ipv6_network_address::less(const ipv6_network_address& other) const noexcept
	{ return is_same_host(other) ? port() < other.port() : m_address < other.m_address; }

	std::string ipv6_network_address::str() const
	{ return std::string("[") + host_str() + "]:" + std::to_string(port()); }

	std::string ipv6_network_address::host_str() const { return m_address.to_string(); }
	bool ipv6_network_address::is_loopback() const { return m_address.is_loopback(); }
	bool ipv6_network_address::is_local() const { return m_address.is_link_local(); }


	bool ipv4_network_subnet::equal(const ipv4_network_subnet& other) const noexcept
	{ return is_same_host(other) && m_mask == other.m_mask; }

	bool ipv4_network_subnet::less(const ipv4_network_subnet& other) const noexcept
	{ return subnet() < other.subnet() ? true : (other.subnet() < subnet() ? false : (m_mask < other.m_mask)); }

	std::string ipv4_network_subnet::str() const
	{ return string_tools::get_ip_string_from_int32(subnet()) + "/" + std::to_string(m_mask); }

	std::string ipv4_network_subnet::host_str() const { return string_tools::get_ip_string_from_int32(subnet()) + "/" + std::to_string(m_mask); }
	bool ipv4_network_subnet::is_loopback() const { return net_utils::is_ip_loopback(subnet()); }
	bool ipv4_network_subnet::is_local() const { return net_utils::is_ip_local(subnet()); }
	bool ipv4_network_subnet::matches(const ipv4_network_address &address) const
	{
		if (m_mask == 0)
			return true;
		return (address.ip() & cidr_to_le_mask(m_mask)) == subnet();
	}

	bool ipv4_network_subnet::contains(const ipv4_network_subnet& other) const noexcept
	{
		if (m_mask == 0)
			return true;
		return m_mask <= other.m_mask && (other.subnet() & cidr_to_le_mask(m_mask)) == subnet();
	}

	std::vector<ipv4_network_subnet> ipv4_network_subnet::complement(const ipv4_network_subnet &child) const
	{
		std::vector<ipv4_network_subnet> result;
		if (*this == child)
			return result;

		ipv4_network_subnet cur = *this;
		while (cur.mask() < child.mask())
		{
			const uint8_t new_mask = cur.mask() + 1;
			const ipv4_network_subnet half0(cur.subnet(), new_mask);
			const ipv4_network_subnet half1(cur.subnet() | swap32(1u << (31 - cur.mask())), new_mask);
			if (half0.contains(child))
			{
				result.push_back(half1);
				cur = half0;
			}
			else
			{
				result.push_back(half0);
				cur = half1;
			}
		}
		return result;
	}

	// ipv6_network_subnet implementation

	void ipv6_network_subnet::apply_cidr_mask(boost::asio::ip::address_v6::bytes_type &bytes, uint8_t prefix)
	{
		// IPv6 bytes are big-endian (MSB first), so mask from the left
		for (int i = 0; i < 16; ++i)
		{
			const int bits_remaining = static_cast<int>(prefix) - i * 8;
			if (bits_remaining >= 8)
				continue; // full byte kept
			else if (bits_remaining <= 0)
				bytes[i] = 0;
			else
				bytes[i] &= static_cast<uint8_t>(0xFF << (8 - bits_remaining));
		}
	}

	void ipv6_network_subnet::set_bit(boost::asio::ip::address_v6::bytes_type &bytes, uint8_t bit_pos)
	{
		// bit_pos 0 = MSB of byte[0]
		const uint8_t byte_idx = bit_pos / 8;
		const uint8_t bit_idx = 7 - (bit_pos % 8);
		bytes[byte_idx] |= (1u << bit_idx);
	}

	ipv6_network_subnet::ipv6_network_subnet(const boost::asio::ip::address_v6::bytes_type &ip, uint8_t mask)
		: m_ip(ip), m_mask(mask)
	{
		assert(mask <= 128);
		apply_cidr_mask(m_ip, m_mask);
	}

	ipv6_network_subnet::ipv6_network_subnet(const boost::asio::ip::address_v6 &ip, uint8_t mask)
		: m_ip(ip.to_bytes()), m_mask(mask)
	{
		assert(mask <= 128);
		apply_cidr_mask(m_ip, m_mask);
	}

	bool ipv6_network_subnet::equal(const ipv6_network_subnet& other) const noexcept
	{
		return m_mask == other.m_mask && m_ip == other.m_ip;
	}

	bool ipv6_network_subnet::less(const ipv6_network_subnet& other) const noexcept
	{
		if (m_ip < other.m_ip) return true;
		if (other.m_ip < m_ip) return false;
		return m_mask < other.m_mask;
	}

	bool ipv6_network_subnet::matches(const ipv6_network_address &address) const
	{
		if (m_mask == 0)
			return true;
		auto addr_bytes = address.ip().to_bytes();
		apply_cidr_mask(addr_bytes, m_mask);
		return addr_bytes == m_ip;
	}

	bool ipv6_network_subnet::contains(const ipv6_network_subnet& other) const noexcept
	{
		if (m_mask == 0)
			return true;
		if (m_mask > other.m_mask)
			return false;
		auto other_masked = other.m_ip;
		apply_cidr_mask(other_masked, m_mask);
		return other_masked == m_ip;
	}

	std::vector<ipv6_network_subnet> ipv6_network_subnet::complement(const ipv6_network_subnet &child) const
	{
		std::vector<ipv6_network_subnet> result;
		if (*this == child)
			return result;

		auto cur_ip = subnet();
		uint8_t cur_mask = mask();
		while (cur_mask < child.mask())
		{
			const uint8_t new_mask = cur_mask + 1;
			ipv6_network_subnet half0(cur_ip, new_mask);

			auto half1_ip = cur_ip;
			set_bit(half1_ip, cur_mask);
			ipv6_network_subnet half1(half1_ip, new_mask);

			if (half0.contains(child))
			{
				result.push_back(half1);
				cur_ip = half0.subnet();
				cur_mask = new_mask;
			}
			else
			{
				result.push_back(half0);
				cur_ip = half1.subnet();
				cur_mask = new_mask;
			}
		}
		return result;
	}

	boost::asio::ip::address_v6::bytes_type ipv6_network_subnet::subnet() const
	{
		// m_ip is already masked in the constructor
		return m_ip;
	}

	std::string ipv6_network_subnet::str() const
	{
		return boost::asio::ip::address_v6(m_ip).to_string() + "/" + std::to_string(m_mask);
	}

	std::string ipv6_network_subnet::host_str() const
	{
		return str();
	}

	bool ipv6_network_subnet::is_loopback() const
	{
		return boost::asio::ip::address_v6(m_ip).is_loopback();
	}

	bool ipv6_network_subnet::is_local() const
	{
		return boost::asio::ip::address_v6(m_ip).is_link_local();
	}


	bool network_address::equal(const network_address& other) const
	{
		// clang typeid workaround
		network_address::interface const* const self_ = self.get();
		network_address::interface const* const other_self = other.self.get();
		if (self_ == other_self) return true;
		if (!self_ || !other_self) return false;
		if (typeid(*self_) != typeid(*other_self)) return false;
		return self_->equal(*other_self);
	}

	bool network_address::less(const network_address& other) const
	{
		// clang typeid workaround
		network_address::interface const* const self_ = self.get();
		network_address::interface const* const other_self = other.self.get();
		if (self_ == other_self) return false;
		if (!self_ || !other_self) return self == nullptr;
		if (typeid(*self_) != typeid(*other_self))
			return self_->get_type_id() < other_self->get_type_id();
		return self_->less(*other_self);
	}

	bool network_address::is_same_host(const network_address& other) const
	{
		// clang typeid workaround
		network_address::interface const* const self_ = self.get();
		network_address::interface const* const other_self = other.self.get();
		if (self_ == other_self) return true;
		if (!self_ || !other_self) return false;
		if (typeid(*self_) == typeid(*other_self))
			return self_->is_same_host(*other_self);
		const auto this_id = get_type_id();
		if (this_id == ipv4_network_address::get_type_id() && other.get_type_id() == ipv6_network_address::get_type_id())
		{
			const boost::asio::ip::address_v6 &actual_ip = other.as<const epee::net_utils::ipv6_network_address>().ip();
			if (actual_ip.is_v4_mapped())
			{
				const uint32_t v4ip = make_address_v4_from_v6(actual_ip);
				return is_same_host(ipv4_network_address(v4ip, 0));
			}
		}
		else if (this_id == ipv6_network_address::get_type_id() && other.get_type_id() == ipv4_network_address::get_type_id())
		{
			const boost::asio::ip::address_v6 &actual_ip = this->as<const epee::net_utils::ipv6_network_address>().ip();
			if (actual_ip.is_v4_mapped())
			{
				const uint32_t v4ip = make_address_v4_from_v6(actual_ip);
				return other.is_same_host(ipv4_network_address(v4ip, 0));
			}
		}
		return false;
	}

  std::string print_connection_context(const connection_context_base& ctx)
  {
    std::stringstream ss;
    ss << ctx.m_remote_address.str() << " " << ctx.m_connection_id << (ctx.m_is_income ? " INC":" OUT");
    return ss.str();
  }

  std::string print_connection_context_short(const connection_context_base& ctx)
  {
    std::stringstream ss;
    ss << ctx.m_remote_address.str() << (ctx.m_is_income ? " INC":" OUT");
    return ss.str();
  }

  const char* zone_to_string(zone value) noexcept
  {
    switch (value)
    {
    case zone::public_:
      return "public";
    case zone::i2p:
      return "i2p";
    case zone::tor:
      return "tor";
    default:
      break;
    }
    return "invalid";
  }

  zone zone_from_string(const boost::string_ref value) noexcept
  {
    if (value == "public")
      return zone::public_;
    if (value == "i2p")
      return zone::i2p;
    if (value == "tor")
      return zone::tor;
    return zone::invalid;
  }
}}

