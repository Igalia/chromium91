// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/menu/menu_item_view.h"

#include <memory>
#include <string>
#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/compositor/canvas_painter.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/native_theme/themed_vector_icon.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/controls/menu/test_menu_item_view.h"
#include "ui/views/test/menu_test_utils.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_class_properties.h"

namespace views {

using MenuItemViewUnitTest = ViewsTestBase;

TEST_F(MenuItemViewUnitTest, AddAndRemoveChildren) {
  views::TestMenuItemView root_menu;

  auto* item = root_menu.AppendMenuItem(0);

  views::SubmenuView* submenu = root_menu.GetSubmenu();
  ASSERT_TRUE(submenu);
  const auto menu_items = submenu->GetMenuItems();
  ASSERT_EQ(1u, menu_items.size());
  EXPECT_EQ(item, menu_items.front());

  root_menu.RemoveMenuItem(item);

  EXPECT_TRUE(submenu->GetMenuItems().empty());
}

namespace {

// A simple View class that will match its height to the available width.
class SquareView : public views::View {
 public:
  SquareView() = default;
  ~SquareView() override = default;

 private:
  gfx::Size CalculatePreferredSize() const override { return gfx::Size(1, 1); }
  int GetHeightForWidth(int width) const override { return width; }
};

}  // namespace

TEST_F(MenuItemViewUnitTest, TestMenuItemViewWithFlexibleWidthChild) {
  views::TestMenuItemView root_menu;

  // Append a normal MenuItemView.
  views::MenuItemView* label_view = root_menu.AppendMenuItem(1, u"item 1");

  // Append a second MenuItemView that has a child SquareView.
  views::MenuItemView* flexible_view = root_menu.AppendMenuItem(2);
  flexible_view->AddChildView(new SquareView());
  // Set margins to 0 so that we know width should match height.
  flexible_view->SetMargins(0, 0);

  views::SubmenuView* submenu = root_menu.GetSubmenu();

  // The first item should be the label view.
  ASSERT_EQ(label_view, submenu->GetMenuItemAt(0));
  gfx::Size label_size = label_view->GetPreferredSize();

  // The second item should be the flexible view.
  ASSERT_EQ(flexible_view, submenu->GetMenuItemAt(1));
  gfx::Size flexible_size = flexible_view->GetPreferredSize();

  EXPECT_EQ(1, flexible_size.width());

  // ...but it should use whatever space is available to make a square.
  int flex_height = flexible_view->GetHeightForWidth(label_size.width());
  EXPECT_EQ(label_size.width(), flex_height);

  // The submenu should be tall enough to allow for both menu items at the
  // given width.
  EXPECT_EQ(label_size.height() + flex_height,
            submenu->GetPreferredSize().height());
}

// Tests that the top-level menu item with hidden children should contain the
// "(empty)" menu item to display.
TEST_F(MenuItemViewUnitTest, TestEmptyTopLevelWhenAllItemsAreHidden) {
  views::TestMenuItemView root_menu;
  views::MenuItemView* item1 = root_menu.AppendMenuItem(1, u"item 1");
  views::MenuItemView* item2 = root_menu.AppendMenuItem(2, u"item 2");

  // Set menu items to hidden.
  item1->SetVisible(false);
  item2->SetVisible(false);

  SubmenuView* submenu = root_menu.GetSubmenu();
  ASSERT_TRUE(submenu);

  EXPECT_EQ(2u, submenu->children().size());

  // Adds any empty menu items to the menu, if needed.
  root_menu.AddEmptyMenus();

  // Because all of the submenu's children are hidden, an empty menu item should
  // have been added.
  ASSERT_EQ(3u, submenu->children().size());
  auto* empty_item = static_cast<MenuItemView*>(submenu->children().front());
  ASSERT_TRUE(empty_item);
  ASSERT_EQ(MenuItemView::kEmptyMenuItemViewID, empty_item->GetID());
  EXPECT_EQ(l10n_util::GetStringUTF16(IDS_APP_MENU_EMPTY_SUBMENU),
            empty_item->title());
}

// Tests that submenu with hidden children should contain the "(empty)" menu
// item to display.
TEST_F(MenuItemViewUnitTest, TestEmptySubmenuWhenAllChildItemsAreHidden) {
  views::TestMenuItemView root_menu;
  MenuItemView* submenu_item = root_menu.AppendSubMenu(1, u"My Submenu");
  MenuItemView* child1 = submenu_item->AppendMenuItem(1, u"submenu item 1");
  MenuItemView* child2 = submenu_item->AppendMenuItem(2, u"submenu item 2");

  // Set submenu children to hidden.
  child1->SetVisible(false);
  child2->SetVisible(false);

  SubmenuView* submenu = submenu_item->GetSubmenu();
  ASSERT_TRUE(submenu);

  EXPECT_EQ(2u, submenu->children().size());

  // Adds any empty menu items to the menu, if needed.
  EXPECT_FALSE(submenu->HasEmptyMenuItemView());
  root_menu.AddEmptyMenus();
  EXPECT_TRUE(submenu->HasEmptyMenuItemView());
  // Because all of the submenu's children are hidden, an empty menu item should
  // have been added.
  ASSERT_EQ(3u, submenu->children().size());
  auto* empty_item = static_cast<MenuItemView*>(submenu->children().front());
  ASSERT_TRUE(empty_item);
  // Not allowed to add an duplicated empty menu item
  // if it already has an empty menu item.
  root_menu.AddEmptyMenus();
  ASSERT_EQ(3u, submenu->children().size());
  ASSERT_EQ(MenuItemView::kEmptyMenuItemViewID, empty_item->GetID());
  EXPECT_EQ(l10n_util::GetStringUTF16(IDS_APP_MENU_EMPTY_SUBMENU),
            empty_item->title());
}

TEST_F(MenuItemViewUnitTest, UseMnemonicOnPlatform) {
  views::TestMenuItemView root_menu;
  views::MenuItemView* item1 = root_menu.AppendMenuItem(1, u"&Item 1");
  views::MenuItemView* item2 = root_menu.AppendMenuItem(2, u"I&tem 2");

  root_menu.set_has_mnemonics(true);

  if (MenuConfig::instance().use_mnemonics) {
    EXPECT_EQ('i', item1->GetMnemonic());
    EXPECT_EQ('t', item2->GetMnemonic());
  } else {
    EXPECT_EQ(0, item1->GetMnemonic());
    EXPECT_EQ(0, item2->GetMnemonic());
  }
}

TEST_F(MenuItemViewUnitTest, NotifiesSelectedChanged) {
  views::TestMenuItemView root_menu;

  // Append a MenuItemView.
  views::MenuItemView* menu_item_view = root_menu.AppendMenuItem(1, u"item");

  // Verify initial selected state.
  bool is_selected = menu_item_view->IsSelected();
  EXPECT_FALSE(is_selected);

  // Subscribe to be notified of changes to selected state.
  auto subscription =
      menu_item_view->AddSelectedChangedCallback(base::BindLambdaForTesting(
          [&]() { is_selected = menu_item_view->IsSelected(); }));

  // Verify we are notified when the MenuItemView becomes selected.
  menu_item_view->SetSelected(true);
  EXPECT_TRUE(is_selected);

  // Verify we are notified when the MenuItemView becomes deselected.
  menu_item_view->SetSelected(false);
  EXPECT_FALSE(is_selected);
}

class TouchableMenuItemViewTest : public ViewsTestBase {
 public:
  TouchableMenuItemViewTest() = default;
  ~TouchableMenuItemViewTest() override = default;

  void SetUp() override {
    ViewsTestBase::SetUp();
    widget_ = CreateTestWidget();
    widget_->Show();

    menu_delegate_ = std::make_unique<test::TestMenuDelegate>();
    menu_item_view_ = new TestMenuItemView(menu_delegate_.get());
    menu_runner_ = std::make_unique<MenuRunner>(
        menu_item_view_, MenuRunner::USE_TOUCHABLE_LAYOUT);
    menu_runner_->RunMenuAt(widget_.get(), nullptr, gfx::Rect(),
                            MenuAnchorPosition::kTopLeft,
                            ui::MENU_SOURCE_KEYBOARD);
  }

  void TearDown() override {
    widget_->CloseNow();
    ViewsTestBase::TearDown();
  }

  gfx::Size AppendItemAndGetSize(int i, const std::u16string& title) {
    return menu_item_view_->AppendMenuItem(i, title)->GetPreferredSize();
  }

 private:
  std::unique_ptr<test::TestMenuDelegate> menu_delegate_;
  std::unique_ptr<MenuRunner> menu_runner_;
  std::unique_ptr<Widget> widget_;

  // Owned by MenuRunner.
  TestMenuItemView* menu_item_view_ = nullptr;
};

// Test that touchable menu items are sized to fit the menu item titles within
// the allowed minimum and maximum width.
TEST_F(TouchableMenuItemViewTest, MinAndMaxWidth) {
  const int min_menu_width = MenuConfig::instance().touchable_menu_min_width;
  const int max_menu_width = MenuConfig::instance().touchable_menu_max_width;

  // Test a title shorter than the minimum width.
  gfx::Size item1_size = AppendItemAndGetSize(1, u"Item1 Short title");
  EXPECT_EQ(item1_size.width(), min_menu_width);

  // Test a title which is between the min and max allowed widths.
  gfx::Size item2_size =
      AppendItemAndGetSize(2, u"Item2 bigger than min less than max");
  EXPECT_GT(item2_size.width(), min_menu_width);
  EXPECT_LT(item2_size.width(), max_menu_width);

  // Test a title which is longer than the max touchable menu width.
  gfx::Size item3_size = AppendItemAndGetSize(
      3, base::ASCIIToUTF16("Item3 Title that is longer than the maximum "
                            "allowed context menu width"));
  EXPECT_EQ(item3_size.width(), max_menu_width);
}

class MenuItemViewLayoutTest : public ViewsTestBase {
 public:
  MenuItemViewLayoutTest() : test_item_(root_menu_.AppendMenuItem(1)) {}
  ~MenuItemViewLayoutTest() override = default;

 protected:
  MenuItemView* test_item() { return test_item_; }

  void PerformLayout() {
    // SubmenuView does not lay out its children unless it is contained in a
    // view, so make a simple container for it.
    SubmenuView* submenu = root_menu_.GetSubmenu();
    ASSERT_TRUE(submenu->owned_by_client());

    submenu_parent_ = std::make_unique<View>();
    submenu_parent_->AddChildView(submenu);
    submenu_parent_->SetPosition(gfx::Point(0, 0));
    submenu_parent_->SetSize(submenu->GetPreferredSize());
  }

 private:
  TestMenuItemView root_menu_;
  MenuItemView* const test_item_;
  std::unique_ptr<View> submenu_parent_;
};

// Tests that MenuItemView takes into account the child's margins and preferred
// size when laying out in container mode.
TEST_F(MenuItemViewLayoutTest, ContainerLayoutRespectsMarginsAndPreferredSize) {
  // We make our menu item a simple container for our view.
  View* child_view = test_item()->AddChildView(std::make_unique<View>());

  // We want to check that MenuItemView::Layout() respects the child's preferred
  // size and margins.
  const gfx::Size child_size(200, 50);
  const gfx::Insets child_margins(5, 10);
  child_view->SetPreferredSize(child_size);
  child_view->SetProperty(kMarginsKey, child_margins);

  PerformLayout();

  // Get |child_view|'s bounds and check that they align with |child_view|'s
  // margins and preferred size.
  const gfx::Rect child_bounds = child_view->bounds();
  const gfx::Insets actual_margins =
      test_item()->GetContentsBounds().InsetsFrom(child_bounds);
  EXPECT_GE(actual_margins.left(), child_margins.left());
  EXPECT_GE(actual_margins.right(), child_margins.right());
  EXPECT_GE(actual_margins.top(), child_margins.top());
  EXPECT_GE(actual_margins.bottom(), child_margins.bottom());
  EXPECT_EQ(child_bounds.width(), child_size.width());
  EXPECT_EQ(child_bounds.height(), child_size.height());
}

namespace {

// A fake View to check if GetHeightForWidth() is called with the appropriate
// width value.
class FakeView : public View {
 public:
  explicit FakeView(int expected_width) : expected_width_(expected_width) {}
  ~FakeView() override = default;

  int GetHeightForWidth(int width) const override {
    // Simply return a height of 1 for the expected width, and 0 otherwise.
    if (width == expected_width_)
      return 1;
    return 0;
  }

 private:
  const int expected_width_;
};

}  // namespace

// Tests that MenuItemView passes the child's true width to
// GetHeightForWidth. This is related to https://crbug.com/933706 which was
// partially caused by it passing the full menu width rather than the width of
// the child view.
TEST_F(MenuItemViewLayoutTest, ContainerLayoutPassesTrueWidth) {
  const gfx::Size child_size(2, 3);
  const gfx::Insets child_margins(1, 1);
  FakeView* child_view =
      test_item()->AddChildView(std::make_unique<FakeView>(child_size.width()));
  child_view->SetPreferredSize(child_size);
  child_view->SetProperty(kMarginsKey, child_margins);

  PerformLayout();

  // |child_view| should get laid out with width child_size.width, at which
  // point child_view->GetHeightForWidth() should be called with the correct
  // width. FakeView::GetHeightForWidth() will return 1 in this case, and 0
  // otherwise. Our preferred height is also set to 3 to check verify that
  // GetHeightForWidth() is even used.
  EXPECT_EQ(child_view->size().height(), 1);
}

class MenuItemViewPaintUnitTest : public ViewsTestBase {
 public:
  MenuItemViewPaintUnitTest() = default;
  ~MenuItemViewPaintUnitTest() override = default;

  MenuItemView* menu_item_view() { return menu_item_view_; }
  MenuRunner* menu_runner() { return menu_runner_.get(); }
  Widget* widget() { return widget_.get(); }

  // ViewsTestBase implementation.
  void SetUp() override {
    ViewsTestBase::SetUp();
    menu_delegate_ = std::make_unique<test::TestMenuDelegate>();
    menu_item_view_ = new MenuItemView(menu_delegate_.get());

    widget_ = std::make_unique<Widget>();
    Widget::InitParams params = CreateParams(Widget::InitParams::TYPE_POPUP);
    params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
    widget_->Init(std::move(params));
    widget_->Show();

    menu_runner_ = std::make_unique<MenuRunner>(menu_item_view_, 0);
  }

  void TearDown() override {
    widget_->CloseNow();
    ViewsTestBase::TearDown();
  }

 private:
  // Owned by MenuRunner.
  MenuItemView* menu_item_view_;

  std::unique_ptr<test::TestMenuDelegate> menu_delegate_;
  std::unique_ptr<MenuRunner> menu_runner_;
  std::unique_ptr<Widget> widget_;

  DISALLOW_COPY_AND_ASSIGN(MenuItemViewPaintUnitTest);
};

// Provides assertion coverage for painting, secondary label, minor text and
// icons.
TEST_F(MenuItemViewPaintUnitTest, MinorTextAndIconAssertionCoverage) {
  auto AddItem = [this](auto label, auto secondary_label, auto minor_label,
                        auto minor_icon) {
    menu_item_view()->AddMenuItemAt(
        0, 1000, base::ASCIIToUTF16(label), secondary_label, minor_label,
        minor_icon, gfx::ImageSkia(), ui::ThemedVectorIcon(),
        views::MenuItemView::Type::kNormal, ui::NORMAL_SEPARATOR);
  };
  AddItem("No secondary label, no minor content", std::u16string(),
          std::u16string(), ui::ThemedVectorIcon());
  AddItem("No secondary label, minor text only", std::u16string(),
          u"minor text", ui::ThemedVectorIcon());
  AddItem("No secondary label, minor icon only", std::u16string(),
          std::u16string(), ui::ThemedVectorIcon(&views::kMenuCheckIcon));
  AddItem("No secondary label, minor text and icon", std::u16string(),
          u"minor text", ui::ThemedVectorIcon(&views::kMenuCheckIcon));
  AddItem("Secondary label, no minor content", u"secondary label",
          std::u16string(), ui::ThemedVectorIcon());
  AddItem("Secondary label, minor text only", u"secondary label", u"minor text",
          ui::ThemedVectorIcon());
  AddItem("Secondary label, minor icon only", u"secondary label",
          std::u16string(), ui::ThemedVectorIcon(&views::kMenuCheckIcon));
  AddItem("Secondary label, minor text and icon", u"secondary label",
          u"minor text", ui::ThemedVectorIcon(&views::kMenuCheckIcon));

  menu_runner()->RunMenuAt(widget(), nullptr, gfx::Rect(),
                           MenuAnchorPosition::kTopLeft,
                           ui::MENU_SOURCE_KEYBOARD);

  SkBitmap bitmap;
  gfx::Size size = menu_item_view()->GetMirroredBounds().size();
  ui::CanvasPainter canvas_painter(&bitmap, size, 1.f, SK_ColorTRANSPARENT,
                                   false);
  menu_item_view()->GetSubmenu()->Paint(
      PaintInfo::CreateRootPaintInfo(canvas_painter.context(), size));
}

}  // namespace views
