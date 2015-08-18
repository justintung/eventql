/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#include "stx/wallclock.h"
#include "ShopProductCTRStatsScan.h"

using namespace stx;

namespace zbase {

ShopProductCTRStatsScan::ShopProductCTRStatsScan(
    RefPtr<TSDBTableScanSource<JoinedSession>> input,
    RefPtr<ProtoSSTableSink<ShopProductKPIs>> output,
    const ReportParams& params) :
    ReportRDD(input.get(), output.get()),
    input_(input),
    output_(output),
    params_(params) {
  input_->setRequiredFields(
      Set<String> {
        "page_views.time",
        "page_views.item_id",
        "page_views.shop_id",
        "search_queries.time",
        "search_queries.page_type",
        "search_queries.result_items.item_id",
        "search_queries.result_items.position",
        "search_queries.result_items.clicked",
        "search_queries.result_items.shop_id",
        "search_queries.shop_id",
        "cart_items.time",
        "cart_items.item_id",
        "cart_items.shop_id",
        "cart_items.price_cents",
        "cart_items.quantity",
        "cart_items.currency",
        "cart_items.checkout_step"
      });
}

void ShopProductCTRStatsScan::onInit() {
  input_->forEach(
      std::bind(
          &ShopProductCTRStatsScan::onSession,
          this,
          std::placeholders::_1));
}

void ShopProductCTRStatsScan::onFinish() {
  for (auto& ctr : products_map_) {
    output_->addRow(ctr.first, ctr.second);
  }
}

void ShopProductCTRStatsScan::onSession(const JoinedSession& row) {
  for (const auto& sq : row.search_queries()) {

    // N.B. this is a hack that we had to put in b/c shop page detection wasn't
    // implemented in the logjoin  for the first runs. remove as soon as that
    // works and a full session reindex was performed
    auto page_type = sq.page_type();
    if (sq.shop_id() > 0) {
      page_type = PAGETYPE_SHOP_PAGE;
    }

    for (const auto& ri : sq.result_items()) {
      auto product = getKPIs(StringUtil::toString(ri.shop_id()), ri.item_id());
      if (!product) {
        continue;
      }

      switch (page_type) {

        case PAGETYPE_CATALOG_PAGE:
          pb_incr(*product, catalog_listview_impressions, 1);
          if (ri.clicked()) pb_incr(*product, catalog_listview_clicks, 1);
          break;

        case PAGETYPE_SEARCH_PAGE:
          pb_incr(*product, search_listview_impressions, 1);
          if (ri.clicked()) pb_incr(*product, search_listview_clicks, 1);
          break;

        case PAGETYPE_SHOP_PAGE:
          pb_incr(*product, shop_page_listview_impressions, 1);
          if (ri.clicked()) pb_incr(*product, shop_page_listview_clicks, 1);
          break;

        default:
          break;

      }
    }
  }

  for (const auto& iv : row.page_views()) {
    auto prod = getKPIs(
        StringUtil::toString(iv.shop_id()),
        iv.item_id());

    if (!prod) {
      continue;
    }

    pb_incr(*prod, product_page_impressions, 1);
  }

  for (const auto& ci : row.cart_items()) {
    auto prod = getKPIs(
        StringUtil::toString(ci.shop_id()),
        ci.item_id());

    if (!prod) {
      continue;
    }

    pb_incr(*prod, cart_items_count, 1);
  }

}


Option<String> ShopProductCTRStatsScan::cacheKey() const {
  return Some(StringUtil::format(
      "cm.analytics.shop_stats.ShopProductCTRStatsScan~5~$0",
      input_->cacheKey()));
}

ShopProductKPIs* ShopProductCTRStatsScan::getKPIs(
    const String& shop_id,
    const String& product_id) {
  if (shop_id.empty() || shop_id == "0") {
    return nullptr;
  }

  auto key = shop_id + "~" + product_id;
  return &products_map_[key];
}

} // namespace zbase
