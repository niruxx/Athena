//! Read-only source history; raw output remains selectable for diagnostics.
use std::rc::Rc;
use adw::prelude::*;
use crate::{context::Context, util};

pub fn build(ctx: &Rc<Context>) -> adw::ToolbarView {
    let view = adw::ToolbarView::new();
    let header = adw::HeaderBar::builder().title_widget(&adw::WindowTitle::new("History", "")).build();
    let refresh = gtk::Button::builder().icon_name("view-refresh-symbolic").tooltip_text("Reload history").build();
    header.pack_end(&refresh); view.add_top_bar(&header);
    let stack = gtk::Stack::new();
    let switcher = gtk::StackSwitcher::builder().stack(&stack).halign(gtk::Align::Center).build();
    view.add_top_bar(&switcher); view.set_content(Some(&stack));
    let ctx2 = ctx.clone(); let target = stack.clone();
    refresh.connect_clicked(move |_| reload(&ctx2, &target));
    reload(ctx, &stack); view
}

fn reload(ctx: &Rc<Context>, stack: &gtk::Stack) {
    while let Some(child) = stack.first_child() { stack.remove(&child); }
    for (source, backend) in ctx.backends.iter().enumerate() {
        let buffer = gtk::TextBuffer::new(None);
        let text = gtk::TextView::builder().buffer(&buffer).editable(false).cursor_visible(false).monospace(true).wrap_mode(gtk::WrapMode::WordChar).top_margin(12).left_margin(12).right_margin(12).bottom_margin(12).build();
        let scroller = gtk::ScrolledWindow::builder().child(&text).build();
        stack.add_titled(&scroller, Some(backend.key()), ctx.source_name(source));
        buffer.set_text("Loading…");
        let backend = backend.clone();
        util::background(move || backend.recent_history(), move |history| buffer.set_text(if history.is_empty() { "No history available." } else { &history }));
    }
}
