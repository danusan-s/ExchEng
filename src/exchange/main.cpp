#include "exchange/order_server/client_request.hpp"

int main() {
  // Create a client request object
  exchange::MEClientRequest request;

  // Set some example values for the request
  request.m_clientId = 12345;
  request.m_tickerId = 54321;
  request.m_orderId = 67890;
  request.m_price = 1005;
  request.m_quantity = 10;
  request.m_side = Side::BUY;

  std::cout << request.toString() << std::endl;

  return 0;
}
