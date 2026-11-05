//! Each source's repositories (or Flatpak remotes): on/off switches, and
//! adding one.

use std::cell::RefCell;
use std::rc::Rc;

use adw::prelude::*;
use athena_core::RepositoryInfo;

use crate::context::Context;
use crate::{terminal, util};

pub struct RepositoriesPage {
    pub widget: adw::ToolbarView,
    stack: gtk::Stack,
    content: adw::PreferencesPage,
    groups: RefCell<Vec<adw::PreferencesGroup>>,
    ctx: Rc<Context>,
}

impl RepositoriesPage {
    pub fn new(ctx: &Rc<Context>) -> Rc<Self> {
        let header = adw::HeaderBar::builder()
            .title_widget(&adw::WindowTitle::new(&crate::i18n::gettext("Repositories"), ""))
            .build();
        let refresh = gtk::Button::builder()
            .icon_name("view-refresh-symbolic")
            .tooltip_text(crate::i18n::gettext("Reload"))
            .build();
        let update_lists = gtk::Button::builder()
            .icon_name("emblem-synchronizing-symbolic")
            .tooltip_text(crate::i18n::gettext("Update Package Lists"))
            .build();
        header.pack_start(&refresh);
        header.pack_end(&update_lists);

        let content = adw::PreferencesPage::new();
        let stack = gtk::Stack::new();
        stack.add_named(&util::loading_page("Loading repositories…"), Some("loading"));
        stack.add_named(&content, Some("list"));

        let widget = adw::ToolbarView::new();
        widget.add_top_bar(&header);
        widget.set_content(Some(&stack));
        let page = Rc::new(Self {
            widget,
            stack,
            content,
            groups: RefCell::default(),
            ctx: ctx.clone(),
        });
        {
            let page = page.clone();
            refresh.connect_clicked(move |_| page.reload());
        }
        {
            let page = page.clone();
            update_lists.connect_clicked(move |_| {
                let commands: Vec<_> = page.ctx.backends.iter().flat_map(|b| b.refresh_metadata_commands()).collect();
                let ctx = page.ctx.clone();
                terminal::run(&page.ctx.window, "Update package lists", commands, true, move |ok| {
                    ctx.toast(if ok { "Package lists updated" } else { "Couldn't update the package lists" });
                    ctx.packages_changed();
                });
            });
        }
        page.reload();
        page
    }

    pub fn reload(self: &Rc<Self>) {
        self.stack.set_visible_child_name("loading");
        let backends = self.ctx.backends.clone();
        let page = self.clone();
        util::background(
            move || backends.iter().map(|b| b.list_repositories()).collect::<Vec<Vec<RepositoryInfo>>>(),
            move |per_source| page.show(per_source),
        );
    }

    fn show(self: &Rc<Self>, per_source: Vec<Vec<RepositoryInfo>>) {
        for group in self.groups.borrow_mut().drain(..) {
            self.content.remove(&group);
        }
        for (source, repos) in per_source.into_iter().enumerate() {
            let backend = self.ctx.backends[source].clone();
            if repos.is_empty() && backend.repository_add_fields().is_empty() {
                continue; // Snap: one store, nothing to configure
            }
            let group = adw::PreferencesGroup::builder()
                .title(self.ctx.source_name(source))
                .description(if backend.key() == "flatpak" { "Remotes" } else { "Repositories" })
                .build();
            if !backend.repository_add_fields().is_empty() {
                let add = gtk::Button::builder()
                    .icon_name("list-add-symbolic")
                    .tooltip_text(crate::i18n::gettext("Add…"))
                    .valign(gtk::Align::Center)
                    .build();
                add.add_css_class("flat");
                let page = self.clone();
                add.connect_clicked(move |_| page.add_dialog(source));
                group.set_header_suffix(Some(&add));
            }
            for repo in repos {
                let subtitle = if repo.url.is_empty() { String::new() } else { repo.url.clone() };
                let row = adw::SwitchRow::builder()
                    .title(gtk::glib::markup_escape_text(&repo.name).as_str())
                    .subtitle(gtk::glib::markup_escape_text(&subtitle).as_str())
                    .active(repo.enabled)
                    .build();
                let (page, id) = (self.clone(), repo.id.clone());
                row.connect_active_notify(move |row| {
                    let (backend, id, enabled) = (page.ctx.backends[source].clone(), id.clone(), row.is_active());
                    row.set_sensitive(false);
                    let (page, row) = (page.clone(), row.clone());
                    util::background(
                        move || backend.set_repository_enabled(&id, enabled),
                        move |result| {
                            if !result.success {
                                page.ctx.toast(&format!("Couldn't change the repository: {}", result.output.trim()));
                            }
                            page.reload(); // show what actually took effect
                            row.set_sensitive(true);
                        },
                    );
                });
                group.add(&row);
            }
            self.content.add(&group);
            self.groups.borrow_mut().push(group);
        }
        self.stack.set_visible_child_name("list");
    }

    fn add_dialog(self: &Rc<Self>, source: usize) {
        let backend = self.ctx.backends[source].clone();
        let fields = backend.repository_add_fields();
        let dialog = adw::AlertDialog::new(Some(&format!("Add a {} Repository", self.ctx.source_name(source))), None);
        let list = gtk::ListBox::builder().selection_mode(gtk::SelectionMode::None).build();
        list.add_css_class("boxed-list");
        let entries: Vec<(&'static str, adw::EntryRow)> = fields
            .iter()
            .map(|f| {
                let row = adw::EntryRow::builder().title(f.label).build();
                row.set_tooltip_text(Some(f.placeholder));
                list.append(&row);
                (f.key, row)
            })
            .collect();
        dialog.set_extra_child(Some(&list));
        dialog.add_response("cancel", &crate::i18n::gettext("Cancel"));
        dialog.add_response("add", &crate::i18n::gettext("Add"));
        dialog.set_response_appearance("add", adw::ResponseAppearance::Suggested);
        dialog.set_close_response("cancel");
        let page = self.clone();
        dialog.connect_response(Some("add"), move |_, _| {
            let values: Vec<(String, String)> = entries.iter().map(|(k, row)| ((*k).to_owned(), row.text().to_string())).collect();
            let (backend, page) = (backend.clone(), page.clone());
            util::background(
                move || backend.add_repository(&values),
                move |result| {
                    page.ctx.toast(&if result.success {
                        "Repository added".to_owned()
                    } else {
                        format!("Couldn't add it: {}", result.output.trim())
                    });
                    page.reload();
                },
            );
        });
        dialog.present(Some(&self.ctx.window));
    }
}
