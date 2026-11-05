//! Read-only source history; raw output remains selectable for diagnostics.
use crate::{context::Context, util};
use adw::prelude::*;
use std::rc::Rc;

pub fn build(ctx: &Rc<Context>) -> adw::ToolbarView {
    let view = adw::ToolbarView::new();
    let header = adw::HeaderBar::builder()
        .title_widget(&adw::WindowTitle::new(&crate::i18n::gettext("History"), ""))
        .build();
    let refresh = gtk::Button::builder()
        .icon_name("view-refresh-symbolic")
        .tooltip_text(crate::i18n::gettext("Reload history"))
        .build();
    header.pack_end(&refresh);
    view.add_top_bar(&header);
    let stack = gtk::Stack::new();
    let mut labels = vec!["Timeline"];
    labels.extend((0..ctx.backends.len()).map(|s| ctx.source_name(s)));
    let switcher = gtk::DropDown::from_strings(&labels);
    let keys = std::iter::once("timeline").chain(ctx.backends.iter().map(|b| b.key())).collect::<Vec<_>>();
    let target = stack.clone();
    switcher.connect_selected_notify(move |dropdown| {
        if let Some(key) = keys.get(dropdown.selected() as usize) {
            target.set_visible_child_name(key);
        }
    });
    view.add_top_bar(&switcher);
    view.set_content(Some(&stack));
    let ctx2 = ctx.clone();
    let target = stack.clone();
    refresh.connect_clicked(move |_| reload(&ctx2, &target));
    reload(ctx, &stack);
    let (weak_ctx, weak_stack) = (Rc::downgrade(ctx), stack.downgrade());
    ctx.on_packages_changed(move || {
        if let (Some(ctx), Some(stack)) = (weak_ctx.upgrade(), weak_stack.upgrade()) {
            reload(&ctx, &stack);
        }
    });
    view
}

fn reload(ctx: &Rc<Context>, stack: &gtk::Stack) {
    while let Some(child) = stack.first_child() {
        stack.remove(&child);
    }
    let timeline = gtk::TextBuffer::new(None);
    let text = gtk::TextView::builder()
        .buffer(&timeline)
        .editable(false)
        .cursor_visible(false)
        .wrap_mode(gtk::WrapMode::WordChar)
        .top_margin(12)
        .left_margin(12)
        .right_margin(12)
        .bottom_margin(12)
        .build();
    stack.add_titled(&gtk::ScrolledWindow::builder().child(&text).build(), Some("timeline"), "Timeline");
    timeline.set_text(&crate::i18n::gettext("Loading…"));
    let backends = ctx.backends.clone();
    util::background(
        move || {
            let mut entries = backends
                .iter()
                .flat_map(|b| athena_core::history::parse(b.key(), &b.recent_history()))
                .collect::<Vec<_>>();
            athena_core::history::sort(&mut entries);
            entries
        },
        move |entries| {
            let text = entries
                .iter()
                .map(|e| format!("{} · {}\n{}", e.date, e.source, e.summary))
                .collect::<Vec<_>>()
                .join("\n\n");
            timeline.set_text(if text.is_empty() { "No recent activity available." } else { &text });
        },
    );
    for (source, backend) in ctx.backends.iter().enumerate() {
        let buffer = gtk::TextBuffer::new(None);
        let text = gtk::TextView::builder()
            .buffer(&buffer)
            .editable(false)
            .cursor_visible(false)
            .monospace(true)
            .wrap_mode(gtk::WrapMode::WordChar)
            .top_margin(12)
            .left_margin(12)
            .right_margin(12)
            .bottom_margin(12)
            .build();
        let scroller = gtk::ScrolledWindow::builder().child(&text).build();
        stack.add_titled(&scroller, Some(backend.key()), ctx.source_name(source));
        buffer.set_text(&crate::i18n::gettext("Loading…"));
        let backend = backend.clone();
        util::background(
            move || backend.recent_history(),
            move |history| buffer.set_text(if history.is_empty() { "No history available." } else { &history }),
        );
    }
}
