use anyhow::{Context, Result};
const CP: [char; 32] = [
    '€', '\u{81}', '‚', 'ƒ', '„', '…', '†', '‡', 'ˆ', '‰', 'Š', '‹', 'Œ', '\u{8d}', 'Ž', '\u{8f}',
    '\u{90}', '‘', '’', '“', '”', '•', '–', '—', '˜', '™', 'š', '›', 'œ', '\u{9d}', 'ž', 'Ÿ',
];
pub struct Text {
    pub text: String,
    encoding: u8,
}
impl Text {
    pub fn decode(bytes: &[u8]) -> Result<Self> {
        let (text, encoding) =
            if bytes.starts_with(&[0xff, 0xfe]) || bytes.starts_with(&[0xfe, 0xff]) {
                anyhow::ensure!(bytes.len().is_multiple_of(2), "UTF-16 incompleto");
                let little = bytes[0] == 0xff;
                let units: Vec<u16> = bytes[2..]
                    .as_chunks::<2>()
                    .0
                    .iter()
                    .map(|b| {
                        if little {
                            u16::from_le_bytes([b[0], b[1]])
                        } else {
                            u16::from_be_bytes([b[0], b[1]])
                        }
                    })
                    .collect();
                (
                    String::from_utf16(&units).context("UTF-16 non valido")?,
                    if little { 2 } else { 3 },
                )
            } else if let Ok(text) = std::str::from_utf8(bytes) {
                (
                    text.strip_prefix('\u{feff}').unwrap_or(text).to_string(),
                    if bytes.starts_with(&[0xef, 0xbb, 0xbf]) {
                        1
                    } else {
                        0
                    },
                )
            } else {
                (
                    bytes
                        .iter()
                        .map(|b| {
                            if (0x80..=0x9f).contains(b) {
                                CP[(b - 0x80) as usize]
                            } else {
                                char::from(*b)
                            }
                        })
                        .collect(),
                    4,
                )
            };
        anyhow::ensure!(
            !text
                .chars()
                .any(|c| c.is_control() && !['\n', '\r', '\t', '\u{c}'].contains(&c)),
            "Dati binari o codifica non supportata"
        );
        Ok(Self { text, encoding })
    }
    pub fn encode(&self, text: &str) -> Result<Vec<u8>> {
        Ok(match self.encoding {
            0 => text.as_bytes().to_vec(),
            1 => [&[0xef, 0xbb, 0xbf], text.as_bytes()].concat(),
            2 | 3 => {
                let little = self.encoding == 2;
                let mut b = if little {
                    vec![0xff, 0xfe]
                } else {
                    vec![0xfe, 0xff]
                };
                for c in text.encode_utf16() {
                    b.extend(if little {
                        c.to_le_bytes()
                    } else {
                        c.to_be_bytes()
                    });
                }
                b
            }
            _ => {
                let mut b = Vec::new();
                for c in text.chars() {
                    if (c as u32) < 128 || ((c as u32) >= 160 && (c as u32) <= 255) {
                        b.push(c as u8);
                    } else if let Some(i) = CP.iter().position(|v| *v == c) {
                        b.push(i as u8 + 128);
                    } else {
                        anyhow::bail!("Carattere non rappresentabile in Windows-1252: {c}");
                    }
                }
                b
            }
        })
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn roundtrip_encodings() {
        for b in [
            b"normal".to_vec(),
            vec![0xef, 0xbb, 0xbf, b'a'],
            vec![0xff, 0xfe, 97, 0, 0x3d, 0xd8, 0, 0xde],
            vec![0xfe, 0xff, 0, 97],
            vec![0xe8, 0x80],
        ] {
            let d = Text::decode(&b).unwrap();
            assert_eq!(d.encode(&d.text).unwrap(), b);
        }
    }
    #[test]
    fn reject_binary_and_lossy_save() {
        assert!(Text::decode(&[0, 1, 2]).is_err());
        assert!(Text::decode(&[0xff, 0xfe, 0]).is_err());
        assert!(Text::decode(&[0xe8]).unwrap().encode("日本").is_err());
    }
}
