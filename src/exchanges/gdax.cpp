#include "gdax.h"
#include "parameters.h"
#include "utils/restapi.h"
#include "utils/base64.h"
#include "hex_str.hpp"
#include "unique_json.hpp"

#include "openssl/sha.h"
#include "openssl/hmac.h"

namespace GDAX {

static RestApi& queryHandle(Parameters &params)
{
  static RestApi query ("https://api.gdax.com",
                        params.cacert.c_str(), *params.logFile);
  return query;
}
	
static json_t* checkResponse(std::ostream &logFile, json_t *root)
{

  logFile << "<GDAX> Dump: "
          << json_dumps(root, 0) << '\n';

	auto msg = json_object_get(root, "message");
  if (!msg) msg = json_object_get(root, "error");

  if (msg)
    logFile << "<GDAX> Error with response: "
            << json_string_value(msg) << '\n';

  return root;
}
	
quote_t getQuote(Parameters &params)
{
  auto &exchange = queryHandle(params);
  unique_json root { exchange.getRequest("/products/BTC-EUR/ticker") };

  const char *bid, *ask;
  int unpack_fail = json_unpack(root.get(), "{s:s, s:s}", "bid", &bid, "ask", &ask);
  if (unpack_fail)
  {
    bid = "0";
    ask = "0";
  }

  return std::make_pair(std::stod(bid), std::stod(ask));
}

double getAvail(Parameters &params, std::string currency)
{

  double availability = 0.0;
#if 1
  unique_json root { authGETRequest(params, "/accounts", "") };

  *params.logFile << "<GDAX> === " << std::endl;

  for (size_t i = json_array_size(root.get()); i--;)
  {
    const char *each_id, *each_currency, *each_amount, *each_hold, *each_balance, *each_profid;
    json_error_t err;
    int unpack_fail = json_unpack_ex (json_array_get(root.get(), i),
                                      &err, 0,
                                      "{s:s, s:s, s:s, s:s, s:s, s:s, s:s}",
                                      "id", &each_id,
                                      "currency", &each_currency,
                                      "available", &each_amount,
                                      "balance", &each_balance,
                                      "hold", &each_hold,
                                      "balance", &each_balance,
                                      "profile_id", &each_profid
                                      );

    *params.logFile << "<GDAX> Curr: " << each_currency
                    <<       " Amount: " << each_amount << std::endl;

    if (unpack_fail)
    {
      *params.logFile << "<GDAX> Error with JSON: "
                      << err.text << std::endl;
    }
    else if (each_currency == currency)
    {
      availability = std::stod(each_amount);
      break;
    }
  }
#endif	
  return availability;
}



std::string sendLongOrder(Parameters& params, std::string direction, double quantity, double price)
{
  return sendOrder(params, direction, quantity, price);
}

std::string sendShortOrder(Parameters& params, std::string direction, double quantity, double price)
{
  return sendOrder(params, direction, quantity, price);
}

std::string sendOrder(Parameters& params, std::string direction, double quantity, double price)
{
  *params.logFile << "<GDAX> Trying to send a \"" << direction << "\" limit order: "
                  << std::setprecision(6) << quantity << "@$"
                  << std::setprecision(2) << price << "...\n";

  std::ostringstream oss;
  oss << "\"product_id\":\"btceur\", "
      << "\"size\":\"" << quantity << "\", "
      << "\"price\":\""  << price    << "\", "
      << "\"side\":\"" << direction << "\", "
      << "\"type\":\"limit\"";

  std::string options = oss.str();
  unique_json root { authPOSTRequest(params, "/orders", options) };
  auto orderId = std::to_string(json_integer_value(json_object_get(root.get(), "id")));
  *params.logFile << "<GDAX> Done (order ID: " << orderId << ")\n" << std::endl;
  return orderId;
}
	
bool isOrderComplete(Parameters& params, std::string orderId)
{
  if (orderId == "0") return true;

  unique_json root { authGETRequest(params, "/orders/" + orderId, "") };

  auto status = json_string_value(json_object_get(root.get(), "status"));
  return status && status == std::string("done");
}

double getActivePos(Parameters &params)
{
  return getAvail(params, "btc");
}

double getLimitPrice(Parameters &params, double volume, bool isBid)
{
  unique_json root { authGETRequest(params, "/products/BTC-EUR/book?level=2", "") };
  json_t *bidask  = json_object_get(root.get(), isBid ? "bids" : "asks");

  *params.logFile << "<GDAX> Looking for a limit price to fill "
                  << std::setprecision(6) << fabs(volume) << " BTC...\n";

  double tmpVol = 0.0;
  double p = 0.0;
  double v;

  // loop on volume
  for (int i = 0, n = json_array_size(bidask); i < n; ++i)
  {
    p = atof(json_string_value(json_object_get(json_array_get(bidask, i), "price")));
    v = atof(json_string_value(json_object_get(json_array_get(bidask, i), "size")));
    *params.logFile << "<GDAX> order book: "
                    << std::setprecision(6) << v << "@$"
                    << std::setprecision(2) << p << std::endl;
    tmpVol += v;
    if (tmpVol >= fabs(volume) * params.orderBookFactor) break;
  }

  // TODO
  return p;
}

json_t* authPOSTRequest(Parameters& params, std::string request, std::string options)
{

  static uint64_t timestamp = time(nullptr);

  std::string method = "POST";
  
  std::string decoded_key = base64_decode(params.gdaxSecret);
  std::string payload_for_signature = std::to_string(timestamp) + method + request + options;

  uint8_t *hmac_digest = HMAC(EVP_sha256(),
                              decoded_key.c_str(), decoded_key.length(),
                              (unsigned char *)payload_for_signature.c_str(), payload_for_signature.size(), NULL, NULL);
  std::string api_sign_header = base64_encode(hmac_digest, SHA256_DIGEST_LENGTH);


#if 0

	// create nonce and POST data
  static uint64_t nonce = time(nullptr) * 4;
  std::string post_data = "nonce=" + std::to_string(++nonce);
  if (!options.empty())
    post_data += "&" + options;

  // Message signature using HMAC-SHA512 of (URI path + SHA256(nonce + POST data))
  // and base64 decoded secret API key
  auto sig_size = request.size() + SHA256_DIGEST_LENGTH;
  std::vector<uint8_t> sig_data(sig_size);
  copy(std::begin(request), std::end(request), std::begin(sig_data));

  std::string payload_for_signature = std::to_string(nonce) + post_data;
  SHA256((uint8_t *)payload_for_signature.c_str(), payload_for_signature.size(),
         &sig_data[ sig_size - SHA256_DIGEST_LENGTH ]);

  std::string decoded_key = base64_decode(params.krakenSecret);
  uint8_t *hmac_digest = HMAC(EVP_sha512(),
                              decoded_key.c_str(), decoded_key.length(),
                              sig_data.data(), sig_data.size(), NULL, NULL);
  std::string api_sign_header = base64_encode(hmac_digest, SHA512_DIGEST_LENGTH);
#endif

  // cURL header
  std::array<std::string, 4> headers
  {
	  "CB-ACCESS-KEY:" + params.gdaxApi,
	  "CB-ACCESS-SIGN:" + api_sign_header,
	  "CB-ACCESS-TIMESTAMP:" + timestamp,
	  "CB-ACCESS-PASSPHRASE:" + params.gdaxPassPhrase,
  };


  // cURL request
  auto &exchange = queryHandle(params);
  return exchange.postRequest(request,
                              make_slist(std::begin(headers), std::end(headers)),
                              options);


}
json_t* authGETRequest(Parameters& params, std::string request, std::string options)
{

  // static uint64_t timestamp = time(nullptr);
  struct timeval tim = { 0 };

  gettimeofday(&tim, NULL);
  //  uint64_t timestamp = (tim.tv_sec * 1000) + (tim.tv_usec / 1000);
  uint64_t timestamp = tim.tv_sec;

  std::string method = "GET";
  
  std::string decoded_key = base64_decode(params.gdaxSecret);
  std::string payload_for_signature = std::to_string(timestamp) + method + request;

  uint8_t *hmac_digest = HMAC(EVP_sha256(),
                              decoded_key.c_str(), decoded_key.length(),
                              (unsigned char *)payload_for_signature.c_str(), payload_for_signature.size(), NULL, NULL);
  std::string api_sign_header = base64_encode(hmac_digest, SHA256_DIGEST_LENGTH);

  // cURL header
  std::array<std::string, 4> headers
  {
	  "CB-ACCESS-KEY:" + params.gdaxApi,
	  "CB-ACCESS-SIGN:" + api_sign_header,
	  "CB-ACCESS-TIMESTAMP:" + std::to_string(timestamp),
	  "CB-ACCESS-PASSPHRASE:" + params.gdaxPassPhrase,
  };

  *params.logFile << "NONCE: " << timestamp << std::endl;

  
  // cURL request
  auto &exchange = queryHandle(params);
  auto root = exchange.getRequest(request,
                                  make_slist(std::begin(headers), std::end(headers)));
  return checkResponse(*params.logFile, root);


}

	
}
