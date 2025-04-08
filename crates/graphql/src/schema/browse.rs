use std::{env, fs};

use async_graphql::*;

use crate::{schema::objects::entry::Entry, AUDIO_EXTENSIONS};

#[derive(Default)]
pub struct BrowseQuery;

#[Object]
impl BrowseQuery {
    async fn tree_get_entries(
        &self,
        _ctx: &Context<'_>,
        path: Option<String>,
    ) -> Result<Vec<Entry>, Error> {
        let show_hidden = false;
        let home = env::var("HOME").unwrap();
        let music_library = env::var("ROCKBOX_LIBRARY").unwrap_or(format!("{}/Music", home));

        let path = match path {
            Some(path) => path,
            None => music_library,
        };

        if !fs::metadata(&path)?.is_dir() {
            return Err(Error::new("Path is not a directory"));
        }

        let mut entries = vec![];

        for file in fs::read_dir(&path)? {
            let file = file?;

            if file.metadata()?.is_file()
                && !AUDIO_EXTENSIONS.iter().any(|ext| {
                    file.path()
                        .to_string_lossy()
                        .ends_with(&format!(".{}", ext))
                })
            {
                continue;
            }

            if file.file_name().to_string_lossy().starts_with(".") && !show_hidden {
                continue;
            }

            entries.push(Entry {
                name: file.path().to_string_lossy().to_string(),
                time_write: file
                    .metadata()?
                    .modified()?
                    .duration_since(std::time::SystemTime::UNIX_EPOCH)?
                    .as_secs() as u32,
                attr: match file.metadata()?.is_dir() {
                    true => 0x10,
                    false => 0,
                },
                ..Default::default()
            });
        }

        Ok(entries)
    }
}
