//! Dashboard summaries load independently and never request authentication.
use crate::{context::Context, util};
use adw::prelude::*;
use athena_core::{cleanup, snapshots};
use std::{
    cell::{Cell, RefCell},
    rc::Rc,
};

pub fn build(ctx: &Rc<Context>) -> adw::ToolbarView {
    let view = adw::ToolbarView::new();
    let header = adw::HeaderBar::builder()
        .title_widget(&adw::WindowTitle::new(&crate::i18n::gettext("Home"), ""))
        .build();
    let refresh = gtk::Button::builder()
        .icon_name("view-refresh-symbolic")
        .tooltip_text(crate::i18n::gettext("Refresh dashboard"))
        .build();
    header.pack_end(&refresh);
    view.add_top_bar(&header);
    let page = adw::PreferencesPage::new();
    let updates = adw::PreferencesGroup::builder()
        .title(crate::i18n::gettext("Software"))
        .description(crate::i18n::gettext(
            "Pending updates and installed totals, by source. AUR totals are also included in pacman's installed total.",
        ))
        .build();
    let counts = Rc::new(RefCell::new(vec![(0, 0); ctx.backends.len()]));
    let remaining = Rc::new(Cell::new(0usize));
    let update = gtk::Button::builder()
        .label(crate::i18n::gettext("Update Everything…"))
        .valign(gtk::Align::Center)
        .sensitive(false)
        .build();
    update.add_css_class("suggested-action");
    updates.set_header_suffix(Some(&update));
    let (ctx2, counts2) = (ctx.clone(), counts.clone());
    update.connect_clicked(move |_| crate::actions::update_everything(&ctx2, counts2.borrow().clone()));
    let rows = ctx
        .backends
        .iter()
        .enumerate()
        .map(|(source, b)| {
            let row = adw::ActionRow::builder()
                .title(ctx.source_name(source))
                .subtitle(crate::i18n::gettext("Open Home to check updates and installed totals"))
                .build();
            if b.key() == "aur" {
                let review = gtk::Button::builder()
                    .label(crate::i18n::gettext("Review Updates…"))
                    .valign(gtk::Align::Center)
                    .build();
                let ctx = ctx.clone();
                review.connect_clicked(move |_| crate::actions::transaction(&ctx, source, crate::actions::Kind::Update, vec![]));
                row.add_suffix(&review);
            }
            updates.add(&row);
            row
        })
        .collect::<Vec<_>>();
    page.add(&updates);
    let storage = adw::PreferencesGroup::builder().title(crate::i18n::gettext("Storage and recovery")).build();
    let cache = adw::ActionRow::builder()
        .title(crate::i18n::gettext("Package cache"))
        .subtitle(crate::i18n::gettext("Not checked yet"))
        .build();
    let clean = gtk::Button::builder()
        .label(crate::i18n::gettext("Free Up Space…"))
        .valign(gtk::Align::Center)
        .build();
    let ctx2 = ctx.clone();
    clean.connect_clicked(move |_| crate::pages::tools::show_cleanup(&ctx2));
    cache.add_suffix(&clean);
    storage.add(&cache);
    let snapshot = adw::ActionRow::builder()
        .title(crate::i18n::gettext("Latest snapshot"))
        .subtitle(crate::i18n::gettext("Not checked yet"))
        .build();
    let manage = gtk::Button::builder()
        .label(crate::i18n::gettext("Snapshots…"))
        .valign(gtk::Align::Center)
        .build();
    let ctx2 = ctx.clone();
    manage.connect_clicked(move |_| crate::pages::tools::show_snapshots(&ctx2));
    snapshot.add_suffix(&manage);
    storage.add(&snapshot);
    page.add(&storage);
    let activity = adw::PreferencesGroup::builder().title(crate::i18n::gettext("Recent activity")).build();
    let all = gtk::Button::with_label(&crate::i18n::gettext("See All"));
    activity.set_header_suffix(Some(&all));
    let ctx2 = ctx.clone();
    all.connect_clicked(move |_| {
        ctx2.navigation.push(
            &adw::NavigationPage::builder()
                .title(crate::i18n::gettext("History"))
                .child(&crate::pages::history::build(&ctx2))
                .build(),
        )
    });
    let history = gtk::Label::builder()
        .label(crate::i18n::gettext("Not checked yet"))
        .xalign(0.)
        .wrap(true)
        .selectable(true)
        .build();
    activity.add(&history);
    page.add(&activity);
    if athena_core::distro::detect_family()==athena_core::distro::DistroFamily::Arch {
        let news=adw::PreferencesGroup::builder().title("Distribution news").build();let ctx2=ctx.clone();news.add(&util::action_row("Arch Linux News","Read upgrade notices in the dedicated news reader.","Read news",move ||crate::pages::arch::show_news(&ctx2)));page.add(&news);
    }
    view.set_content(Some(&page));
    let weak = Rc::downgrade(ctx);
    let busy = Rc::new(Cell::new(false));
    let generation = Rc::new(Cell::new(0u64));
    let reload: Rc<dyn Fn()> = Rc::new(move || {
        let Some(ctx) = weak.upgrade() else {
            return;
        };
        if busy.replace(true) {
            return;
        }
        let revision = generation.get() + 1;
        generation.set(revision);
        remaining.set(ctx.backends.len());
        update.set_sensitive(false);
        for (source, backend) in ctx.backends.iter().enumerate() {
            let backend = backend.clone();
            let (row, counts, remaining, update, busy, generation) = (
                rows[source].clone(),
                counts.clone(),
                remaining.clone(),
                update.clone(),
                busy.clone(),
                generation.clone(),
            );
            row.set_subtitle(&crate::i18n::gettext("Checking…"));
            util::background(
                move || (backend.list_installed().len(), backend.list_updates().len()),
                move |(installed, pending)| {
                    if generation.get() != revision {
                        return;
                    }
                    row.set_subtitle(&crate::i18n::gettext_format(
                        if row.title() == "AUR" {
                            "{installed} installed · {pending} updates waiting for review"
                        } else {
                            "{installed} installed · {pending} pending updates"
                        },
                        &[("installed", installed.to_string()), ("pending", pending.to_string())],
                    ));
                    counts.borrow_mut()[source] = (source, pending);
                    remaining.set(remaining.get().saturating_sub(1));
                    if remaining.get() == 0 {
                        busy.set(false);
                        update.set_sensitive(counts.borrow().iter().any(|(_, n)| *n > 0));
                    }
                },
            );
        }
        let backends = ctx.backends.clone();
        let cache = cache.clone();
        util::background(
            move || {
                backends
                    .iter()
                    .filter_map(|b| cleanup::cache(b.key()))
                    .fold((0u64, 0usize), |(bytes, errors), (area, _)| (bytes + area.bytes(), errors + area.errors))
            },
            move |(bytes, errors)| cache.set_subtitle(&format!("Up to {} · {errors} unreadable paths", cleanup::size(bytes))),
        );
        let snapshot = snapshot.clone();
        util::background(snapshots::summary, move |text| snapshot.set_subtitle(&text));
        let backends = ctx.backends.clone();
        let history = history.clone();
        util::background(
            move || {
                let mut entries = backends
                    .iter()
                    .filter(|b| b.key() != "aur")
                    .flat_map(|b| athena_core::history::parse(b.key(), &b.recent_history()))
                    .collect::<Vec<_>>();
                athena_core::history::sort(&mut entries);
                entries
                    .into_iter()
                    .take(5)
                    .map(|e| format!("{} · {}\n{}", e.date, e.source, e.summary))
                    .collect::<Vec<_>>()
                    .join("\n\n")
            },
            move |text| history.set_label(if text.is_empty() { "No recent activity reported." } else { &text }),
        );
    });
    let reload2 = reload.clone();
    refresh.connect_clicked(move |_| reload2());
    let reload2 = reload.clone();
    view.connect_map(move |_| reload2());
    let weak_view = view.downgrade();
    ctx.on_packages_changed(move || {
        if weak_view.upgrade().is_some_and(|v| v.is_mapped()) {
            reload();
        }
    });
    view
}
