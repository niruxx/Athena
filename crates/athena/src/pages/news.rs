//! A dedicated news reader for the system's distribution (Arch, Fedora,
//! Debian or Ubuntu). Distributions without a known feed don't show it.
use crate::{context::Context, util};
use adw::prelude::*;
use athena_core::arch::{News, NewsFeed};
use std::{
    cell::{Cell, RefCell},
    rc::Rc,
};

const READ_KEY: &str = "arch-news-read";

fn read_ids() -> Vec<String> {
    crate::settings::load().map(|s| s.strv(READ_KEY).iter().map(|v| v.to_string()).collect()).unwrap_or_default()
}

fn mark_read(ids: &[String]) {
    if let Some(settings) = crate::settings::load() {
        let mut known = read_ids();
        for id in ids {
            if !known.contains(id) {
                known.push(id.clone());
            }
        }
        let _ = settings.set_strv(READ_KEY, known.as_slice());
    }
}

/// "Tue, 04 Nov 2026 10:00:00 +0000" → "04 Nov 2026"; other formats pass through.
fn short_date(date: &str) -> String {
    let parts: Vec<_> = date.split_whitespace().collect();
    if parts.len() >= 4 && parts[0].ends_with(',') {
        format!("{} {} {}", parts[1], parts[2], parts[3])
    } else {
        date.split('T').next().unwrap_or(date).to_owned()
    }
}

fn excerpt(text: &str) -> String {
    let flat = text.split_whitespace().collect::<Vec<_>>().join(" ");
    if flat.chars().count() > 220 { format!("{}…", flat.chars().take(220).collect::<String>().trim_end()) } else { flat }
}

struct Reader {
    stack: gtk::Stack,
    article: gtk::Box,
    back: gtk::Button,
    previous: gtk::Button,
    next: gtk::Button,
    articles: RefCell<Vec<News>>,
    current: Cell<usize>,
    cards: RefCell<Vec<gtk::FlowBoxChild>>,
    feed: NewsFeed,
}

impl Reader {
    fn open(self: &Rc<Self>, index: usize) {
        let Some(article) = self.articles.borrow().get(index).cloned() else { return };
        self.current.set(index);
        while let Some(child) = self.article.first_child() {
            self.article.remove(&child);
        }
        let meta = gtk::Box::builder().spacing(8).build();
        let source = util::label(self.feed.site, &["stat-pill"]);
        meta.append(&source);
        meta.append(&util::label(&short_date(&article.date), &["stat-pill", "numeric"]));
        self.article.append(&meta);
        let title = gtk::Label::builder().label(&article.title).wrap(true).xalign(0.).selectable(true).build();
        title.add_css_class("article-title");
        self.article.append(&title);
        self.article.append(&gtk::Separator::new(gtk::Orientation::Horizontal));
        let paragraphs: Vec<_> = article.description.split("\n\n").map(str::trim).filter(|t| !t.is_empty()).collect();
        if paragraphs.is_empty() {
            self.article.append(&util::label("This notice has no summary. Open the original to read it.", &["dim-label"]));
        }
        for paragraph in paragraphs {
            let text = gtk::Label::builder()
                .label(paragraph)
                .wrap(true)
                .wrap_mode(gtk::pango::WrapMode::WordChar)
                .selectable(true)
                .xalign(0.)
                .build();
            text.add_css_class("article-body");
            self.article.append(&text);
        }
        let open = gtk::Button::builder().label(format!("Read on {}", self.feed.site)).halign(gtk::Align::Start).margin_top(8).build();
        open.add_css_class("pill");
        open.add_css_class("suggested-action");
        let url = article.url.clone();
        open.connect_clicked(move |button| {
            gtk::UriLauncher::new(&url).launch(button.root().and_downcast_ref::<gtk::Window>(), None::<&gtk::gio::Cancellable>, |_| {});
        });
        self.article.append(&open);
        mark_read(std::slice::from_ref(&article.id));
        if let Some(card) = self.cards.borrow().get(index) {
            set_card_read(card);
        }
        let count = self.articles.borrow().len();
        self.previous.set_sensitive(index > 0);
        self.next.set_sensitive(index + 1 < count);
        self.stack.set_transition_type(gtk::StackTransitionType::SlideLeft);
        self.stack.set_visible_child_name("reader");
        for button in [&self.back, &self.previous, &self.next] {
            button.set_visible(true);
        }
        util::animate(&self.article, "rise-in");
    }

    fn close(&self) {
        self.stack.set_transition_type(gtk::StackTransitionType::SlideRight);
        self.stack.set_visible_child_name("feed");
        for button in [&self.back, &self.previous, &self.next] {
            button.set_visible(false);
        }
    }
}

fn set_card_read(card: &gtk::FlowBoxChild) {
    card.set_widget_name("read");
    if let Some(inner) = card.child() {
        inner.remove_css_class("unread");
        if let Some(header) = inner.first_child()
            && let Some(dot) = header.first_child()
        {
            dot.set_visible(false);
        }
    }
}

pub fn build(ctx: &Rc<Context>, feed: NewsFeed) -> adw::ToolbarView {
    let view = adw::ToolbarView::new();
    let header = adw::HeaderBar::builder()
        .title_widget(&adw::WindowTitle::new(feed.name, &format!("Notices from {}", feed.site)))
        .build();
    let back = gtk::Button::builder().icon_name("go-previous-symbolic").tooltip_text("All articles").visible(false).build();
    let previous = gtk::Button::builder().icon_name("go-up-symbolic").tooltip_text("Newer article").visible(false).build();
    let next = gtk::Button::builder().icon_name("go-down-symbolic").tooltip_text("Older article").visible(false).build();
    header.pack_start(&back);
    let refresh = gtk::Button::builder().icon_name("view-refresh-symbolic").tooltip_text("Refresh news").build();
    let mark_all = gtk::Button::builder().icon_name("mail-read-symbolic").tooltip_text("Mark all as read").build();
    header.pack_end(&refresh);
    header.pack_end(&mark_all);
    header.pack_end(&next);
    header.pack_end(&previous);
    view.add_top_bar(&header);

    let stack = gtk::Stack::builder().transition_type(gtk::StackTransitionType::Crossfade).transition_duration(260).build();

    // The feed: a search box, filters, and a grid of article cards.
    let search = gtk::SearchEntry::builder().placeholder_text("Find a notice").hexpand(true).build();
    let unread = gtk::ToggleButton::with_label("Unread only");
    unread.add_css_class("filter-chip");
    let controls = gtk::Box::builder().spacing(12).build();
    controls.append(&search);
    controls.append(&unread);
    let summary = util::label("", &["dim-label"]);
    let cards = gtk::FlowBox::builder()
        .selection_mode(gtk::SelectionMode::None)
        .min_children_per_line(1)
        .max_children_per_line(2)
        .column_spacing(14)
        .row_spacing(14)
        .homogeneous(true)
        .activate_on_single_click(true)
        .build();
    let feed_box = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(18).margin_top(22).margin_bottom(28).margin_start(20).margin_end(20).build();
    let intro = gtk::Label::builder()
        .label(if feed.site == "archlinux.org" {
            "Read upgrade notices before updating your Arch system — some require manual steps."
        } else {
            "The latest announcements from your distribution."
        })
        .xalign(0.)
        .wrap(true)
        .build();
    intro.add_css_class("title-4");
    feed_box.append(&intro);
    feed_box.append(&controls);
    feed_box.append(&summary);
    feed_box.append(&cards);
    stack.add_named(
        &gtk::ScrolledWindow::builder()
            .child(&adw::Clamp::builder().maximum_size(1000).child(&feed_box).build())
            .hscrollbar_policy(gtk::PolicyType::Never)
            .build(),
        Some("feed"),
    );

    // The reader.
    let article = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(18).build();
    article.add_css_class("card");
    article.add_css_class("news-article");
    stack.add_named(
        &gtk::ScrolledWindow::builder()
            .child(
                &adw::Clamp::builder()
                    .maximum_size(780)
                    .margin_top(20)
                    .margin_bottom(32)
                    .margin_start(16)
                    .margin_end(16)
                    .child(&article)
                    .build(),
            )
            .hscrollbar_policy(gtk::PolicyType::Never)
            .build(),
        Some("reader"),
    );
    stack.add_named(&util::loading_page(&format!("Fetching {}…", feed.name)), Some("loading"));
    let error = adw::StatusPage::builder()
        .icon_name("network-offline-symbolic")
        .title("News unavailable")
        .description("Check your connection, then refresh to try again.")
        .build();
    stack.add_named(&error, Some("error"));
    view.set_content(Some(&stack));

    let reader = Rc::new(Reader {
        stack: stack.clone(),
        article,
        back: back.clone(),
        previous: previous.clone(),
        next: next.clone(),
        articles: RefCell::default(),
        current: Cell::new(0),
        cards: RefCell::default(),
        feed: feed.clone(),
    });

    let (query, only_unread) = (Rc::new(RefCell::new(String::new())), Rc::new(Cell::new(false)));
    {
        let (query, only_unread) = (query.clone(), only_unread.clone());
        cards.set_filter_func(move |card| {
            (!only_unread.get() || card.widget_name() == "unread")
                && card.tooltip_text().is_none_or(|text| text.to_lowercase().contains(query.borrow().as_str()))
        });
    }
    {
        let cards = cards.clone();
        search.connect_search_changed(move |entry| {
            *query.borrow_mut() = entry.text().to_lowercase();
            cards.invalidate_filter();
        });
    }
    {
        let cards = cards.clone();
        unread.connect_toggled(move |button| {
            only_unread.set(button.is_active());
            cards.invalidate_filter();
        });
    }
    {
        let reader = reader.clone();
        back.connect_clicked(move |_| reader.close());
    }
    {
        let reader = reader.clone();
        previous.connect_clicked(move |_| {
            let index = reader.current.get();
            if index > 0 {
                reader.open(index - 1);
            }
        });
    }
    {
        let reader = reader.clone();
        next.connect_clicked(move |_| reader.open(reader.current.get() + 1));
    }
    {
        let (reader, cards, summary) = (reader.clone(), cards.clone(), summary.clone());
        mark_all.connect_clicked(move |_| {
            let ids: Vec<_> = reader.articles.borrow().iter().map(|a| a.id.clone()).collect();
            mark_read(&ids);
            for card in reader.cards.borrow().iter() {
                set_card_read(card);
            }
            summary.set_label(&format!("{} articles · all read", ids.len()));
            cards.invalidate_filter();
        });
    }
    {
        let reader = reader.clone();
        cards.connect_child_activated(move |_, card| reader.open(card.index() as usize));
    }

    let loaded = Rc::new(Cell::new(false));
    let loading = Rc::new(Cell::new(false));
    let reload: Rc<dyn Fn()> = Rc::new({
        let (cards, stack, reader, refresh, summary) = (cards.clone(), stack.clone(), reader.clone(), refresh.clone(), summary.clone());
        move || {
            if loading.replace(true) {
                return;
            }
            refresh.set_sensitive(false);
            reader.close();
            stack.set_visible_child_name("loading");
            let (cards, stack, reader, refresh, summary, loading, error) =
                (cards.clone(), stack.clone(), reader.clone(), refresh.clone(), summary.clone(), loading.clone(), error.clone());
            let feed = reader.feed.clone();
            util::background(
                move || athena_core::arch::fetch_news(&feed),
                move |result| {
                    loading.set(false);
                    refresh.set_sensitive(true);
                    let articles = match result {
                        Err(message) => {
                            error.set_description(Some(&message));
                            stack.set_visible_child_name("error");
                            return;
                        }
                        Ok(articles) => articles,
                    };
                    cards.remove_all();
                    let read = read_ids();
                    let mut children = Vec::new();
                    let unread_count = articles.iter().filter(|a| !read.contains(&a.id)).count();
                    summary.set_label(&format!(
                        "{} articles · {}",
                        articles.len(),
                        if unread_count == 0 { "all read".to_owned() } else { format!("{unread_count} unread") }
                    ));
                    for article in &articles {
                        let is_read = read.contains(&article.id);
                        let card = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(10).build();
                        card.add_css_class("card");
                        card.add_css_class("news-card");
                        if !is_read {
                            card.add_css_class("unread");
                        }
                        let top = gtk::Box::builder().spacing(8).build();
                        let dot = gtk::Box::builder().valign(gtk::Align::Center).visible(!is_read).build();
                        dot.add_css_class("unread-dot");
                        top.append(&dot);
                        let date = util::label(&short_date(&article.date), &["caption", "dim-label", "numeric"]);
                        date.set_hexpand(true);
                        top.append(&date);
                        top.append(&gtk::Image::from_icon_name("go-next-symbolic"));
                        card.append(&top);
                        let title = gtk::Label::builder()
                            .label(&article.title)
                            .xalign(0.)
                            .wrap(true)
                            .lines(2)
                            .ellipsize(gtk::pango::EllipsizeMode::End)
                            .build();
                        title.add_css_class("news-title");
                        card.append(&title);
                        let body = gtk::Label::builder()
                            .label(excerpt(&article.description))
                            .xalign(0.)
                            .yalign(0.)
                            .wrap(true)
                            .wrap_mode(gtk::pango::WrapMode::WordChar)
                            .lines(3)
                            .ellipsize(gtk::pango::EllipsizeMode::End)
                            .vexpand(true)
                            .build();
                        body.add_css_class("dim-label");
                        card.append(&body);
                        let child = gtk::FlowBoxChild::builder().child(&card).tooltip_text(&article.title).build();
                        child.set_widget_name(if is_read { "read" } else { "unread" });
                        cards.insert(&child, -1);
                        children.push(child);
                    }
                    *reader.cards.borrow_mut() = children;
                    *reader.articles.borrow_mut() = articles;
                    stack.set_visible_child_name("feed");
                    util::animate(&cards, "rise-in");
                },
            );
        }
    });
    {
        let reload = reload.clone();
        refresh.connect_clicked(move |_| reload());
    }
    view.connect_map(move |_| {
        if !loaded.replace(true) {
            reload();
        }
    });
    let _ = ctx;
    view
}
