#include "sodium.h"
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <asio/ts/io_context.hpp>
#include <asio/ts/net.hpp>

class dvlnet {
public:
	typedef std::vector<unsigned char> buffer_t;
	static std::unique_ptr<dvlnet> inst;

	virtual int create(std::string addrstr, std::string passwd) = 0;
	virtual int join(std::string addrstr, std::string passwd) = 0;
	virtual bool SNetReceiveMessage(int *sender, char **data, int *size) = 0;
	virtual bool SNetSendMessage(int dest, void *data, unsigned int size) = 0;
	virtual bool SNetReceiveTurns(char **data, unsigned int *size, DWORD *status) = 0;
	virtual bool SNetSendTurn(char *data, unsigned int size) = 0;
	virtual int SNetGetProviderCaps(struct _SNETCAPS *caps) = 0;
	virtual void *SNetRegisterEventHandler(event_type evtype, void(__stdcall *func)(struct _SNETEVENT *)) = 0;
	virtual void *SNetUnregisterEventHandler(event_type evtype, void(__stdcall *func)(struct _SNETEVENT *)) = 0;
	virtual ~dvlnet() {}
};

class dvlnet_null: public dvlnet {
private:
	std::queue<buffer_t> message_queue;
	buffer_t message_last;
	const int plr_single = 0;

public:
	virtual int create(std::string addrstr, std::string passwd);
	virtual int join(std::string addrstr, std::string passwd);
	virtual bool SNetReceiveMessage(int *sender, char **data, int *size);
	virtual bool SNetSendMessage(int dest, void *data, unsigned int size);
	virtual bool SNetReceiveTurns(char **data, unsigned int *size, DWORD *status);
	virtual bool SNetSendTurn(char *data, unsigned int size);
	virtual int SNetGetProviderCaps(struct _SNETCAPS *caps);
	virtual void *SNetRegisterEventHandler(event_type evtype, void(__stdcall *func)(struct _SNETEVENT *));
	virtual void *SNetUnregisterEventHandler(event_type evtype, void(__stdcall *func)(struct _SNETEVENT *));
};

// exact meaning yet to be worked out
#define PS_CONNECTED 0x10000
#define PS_TURN_ARRIVED 0x20000
#define PS_ACTIVE 0x40000

class dvlnet_udp : public dvlnet {
public:
	dvlnet_udp(buffer_t info);
	virtual int create(std::string addrstr, std::string passwd);
	virtual int join(std::string addrstr, std::string passwd);

	virtual bool SNetReceiveMessage(int *sender, char **data, int *size);
	virtual bool SNetSendMessage(int dest, void *data, unsigned int size);
	virtual bool SNetReceiveTurns(char **data, unsigned int *size, DWORD *status);
	virtual bool SNetSendTurn(char *data, unsigned int size);
	virtual int SNetGetProviderCaps(struct _SNETCAPS *caps);
	virtual void *SNetRegisterEventHandler(event_type evtype, void(__stdcall *func)(struct _SNETEVENT *));
	virtual void *SNetUnregisterEventHandler(event_type evtype, void(__stdcall *func)(struct _SNETEVENT *));

	static constexpr unsigned short max_packet_size = 0xFFFF;

	enum packet_type : uint8_t {
		PT_MESSAGE = 0x01,
		PT_TURN = 0x02,
		PT_JOIN_REQUEST = 0x11,
		PT_JOIN_ACCEPT = 0x12,
		PT_LEAVE_GAME = 0x13,
	};
	typedef uint8_t plr_t;
	typedef uint32_t cookie_t;
	typedef int turn_t; // change int to something else in devilution code later
	typedef std::array<unsigned char, crypto_secretbox_KEYBYTES> key_t;
	class packet_exception : public std::exception {};
	typedef asio::ip::udp::endpoint endpoint;
	static const endpoint none;

	class packet {
	protected:
		packet_type m_type;
		plr_t m_src;
		plr_t m_dest;
		buffer_t m_message;
		turn_t m_turn;
		cookie_t m_cookie;
		plr_t m_newplr;
		plr_t m_oldplr;
		buffer_t m_info;

		const key_t &key;
		bool have_encrypted = false;
		bool have_decrypted = false;
		buffer_t encrypted_buffer;
		buffer_t decrypted_buffer;

	public:
		packet(const key_t &k) : key(k) {};

		const buffer_t &data();

		packet_type type();
		plr_t src();
		plr_t dest();
		const buffer_t &message();
		turn_t turn();
		cookie_t cookie();
		plr_t newplr();
		plr_t oldplr();
		const buffer_t &info();
	};

	template<class P> class packet_proc : public packet {
	public:
		using packet::packet;
		void process_data();
	};

	class packet_in : public packet_proc<packet_in> {
	public:
		using packet_proc<packet_in>::packet_proc;
		void create(buffer_t buf);
		void process_element(buffer_t &x);
		template <class T> void process_element(T &x);
		void decrypt();
	};

	class packet_out : public packet_proc<packet_out> {
	public:
		using packet_proc<packet_out>::packet_proc;
		void create(packet_type t, plr_t s, plr_t d, buffer_t m);
		void create(packet_type t, plr_t s, plr_t d, turn_t u);
		void create(packet_type t, plr_t s, plr_t d, cookie_t c);
		void create(packet_type t, plr_t s, plr_t d, cookie_t c, plr_t n, buffer_t i);
		void create(packet_type t, plr_t s, plr_t d, plr_t o);
		void process_element(buffer_t &x);
		template <class T> void process_element(T &x);
		template <class T> static const unsigned char *begin(const T &x);
		template <class T> static const unsigned char *end(const T &x);
		void encrypt();
	};

	typedef std::unique_ptr<packet> upacket;
	upacket make_packet(buffer_t buf);
	template<typename T, typename... Args> upacket make_packet(T t, Args... args);

private:
	static constexpr unsigned short default_port = 6112;
	static constexpr unsigned short try_ports = 512;
	static constexpr daddr_t ADDR_BROADCAST = 0xFF;
	static constexpr daddr_t ADDR_MASTER = 0xFE;
	static constexpr int ACTIVE = 60;

	std::map<event_type, void(__stdcall *)(struct _SNETEVENT *)> registered_handlers;
	buffer_t game_init_info;

	struct message_t {
		int sender; // change int to something else in devilution code later
		buffer_t payload;
		message_t() : sender(-1), payload({}) {};
		message_t(int s, buffer_t p) : sender(s), payload(p) {};
	};

	message_t message_last;
	std::queue<message_t> message_queue;
	std::array<turn_t, MAX_PLRS> turn_last = { 0 };
	std::array<std::queue<turn_t>, MAX_PLRS> turn_queue;

	plr_t plr_self = ADDR_BROADCAST;
	unsigned short udpport_self = 0;
	cookie_t cookie_self = 0;

	key_t key = { 0 };
	endpoint master;

	std::set<endpoint> connection_requests_pending;
	std::array<endpoint, MAX_PLRS> nexthop_table;
	std::array<int, MAX_PLRS> active_table = { 0 };
	asio::io_context context;
	asio::ip::udp::socket sock = asio::ip::udp::socket(context);

	unsigned short bind();
	void setup_password(std::string pw);

	void handle_join_request(upacket &pkt, endpoint sender);
	void handle_accept(upacket &pkt);
	void recv();
	void send(upacket &pkt, endpoint sender = none);
	void recv_decrypted(upacket &pkt, endpoint sender);
	std::set<endpoint> dests_for_addr(plr_t dest, endpoint sender);
	void run_event_handler(_SNETEVENT &ev);
};

template<class P> void dvlnet_udp::packet_proc<P>::process_data()
{
	P &self = static_cast<P&>(*this);
	self.process_element(m_type);
	self.process_element(m_src);
	self.process_element(m_dest);
	switch (m_type) {
	case PT_MESSAGE:
		self.process_element(m_message);
		break;
	case PT_TURN:
		self.process_element(m_turn);
		break;
	case PT_JOIN_REQUEST:
		self.process_element(m_cookie);
		break;
	case PT_JOIN_ACCEPT:
		self.process_element(m_cookie);
		self.process_element(m_newplr);
		self.process_element(m_info);
		break;
	case PT_LEAVE_GAME:
		break;
	}
}

inline dvlnet_udp::upacket dvlnet_udp::make_packet(buffer_t buf)
{
	auto ret = std::make_unique<packet_in>(key);
	ret->create(std::move(buf));
	ret->decrypt();
	return ret;
}

template<typename T, typename... Args> dvlnet_udp::upacket dvlnet_udp::make_packet(T t, Args... args)
{
	auto ret = std::make_unique<packet_out>(key);
	ret->create(t, args...);
	ret->encrypt();
	return ret;
}

inline void dvlnet_udp::packet_in::process_element(buffer_t &x)
{
	x.insert(x.begin(), decrypted_buffer.begin(), decrypted_buffer.end());
	decrypted_buffer.resize(0);
}

template <class T> void dvlnet_udp::packet_in::process_element(T &x)
{
	if (decrypted_buffer.size() < sizeof(T))
		throw packet_exception();
	x = *reinterpret_cast<T *>(decrypted_buffer.data());
	decrypted_buffer.erase(decrypted_buffer.begin(), decrypted_buffer.begin() + sizeof(T));
}

inline void dvlnet_udp::packet_out::process_element(buffer_t &x)
{
	encrypted_buffer.insert(encrypted_buffer.end(), x.begin(), x.end());
}

template <class T> void dvlnet_udp::packet_out::process_element(T &x)
{
	encrypted_buffer.insert(encrypted_buffer.end(), begin(x), end(x));
}

template <class T> const unsigned char *dvlnet_udp::packet_out::begin(const T &x)
{
	return reinterpret_cast<const unsigned char *>(&x);
}

template <class T> const unsigned char *dvlnet_udp::packet_out::end(const T &x)
{
	return reinterpret_cast<const unsigned char *>(&x) + sizeof(T);
}