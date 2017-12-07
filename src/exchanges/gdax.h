#ifndef GDAX_H
#define GDAX_H

#include "quote_t.h"
#include <string>

struct Parameters;

namespace GDAX {

quote_t getQuote(Parameters &params);

double getAvail(Parameters &params, std::string currency);

std::string sendLongOrder(Parameters& params, std::string direction, double quantity, double price);

std::string sendShortOrder(Parameters& params, std::string direction, double quantity, double price);

std::string sendOrder(Parameters& params, std::string direction, double quantity, double price);

bool isOrderComplete(Parameters& params, std::string orderId);

double getActivePos(Parameters &params);

double getLimitPrice(Parameters &params, double volume, bool isBid);

json_t* authGETRequest(Parameters& params, std::string request, std::string options);
json_t* authPOSTRequest(Parameters& params, std::string request, std::string options);

}

#endif
