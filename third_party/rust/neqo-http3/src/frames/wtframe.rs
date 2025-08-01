// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use neqo_common::{Decoder, Encoder};

use super::hframe::HFrameType;
use crate::{frames::reader::FrameDecoder, Error, Res};

pub type WebTransportFrameType = u64;

const WT_FRAME_CLOSE_SESSION: WebTransportFrameType = 0x2843;
const WT_FRAME_CLOSE_MAX_MESSAGE_SIZE: u64 = 1024;

#[derive(PartialEq, Eq, Debug)]
pub enum WebTransportFrame {
    CloseSession { error: u32, message: String },
}

impl WebTransportFrame {
    pub fn encode(&self, enc: &mut Encoder) {
        enc.encode_varint(WT_FRAME_CLOSE_SESSION);
        let Self::CloseSession { error, message } = &self;
        enc.encode_varint(4 + message.len() as u64);
        enc.encode_uint(4, *error);
        enc.encode(message.as_bytes());
    }
}

impl FrameDecoder<Self> for WebTransportFrame {
    fn decode(frame_type: HFrameType, frame_len: u64, data: Option<&[u8]>) -> Res<Option<Self>> {
        if let Some(payload) = data {
            let mut dec = Decoder::from(payload);
            if frame_type == HFrameType(WT_FRAME_CLOSE_SESSION) {
                if frame_len > WT_FRAME_CLOSE_MAX_MESSAGE_SIZE + 4 {
                    return Err(Error::HttpMessage);
                }
                let error = dec.decode_uint().ok_or(Error::HttpMessage)?;
                let Ok(message) = String::from_utf8(dec.decode_remainder().to_vec()) else {
                    return Err(Error::HttpMessage);
                };
                Ok(Some(Self::CloseSession { error, message }))
            } else {
                Ok(None)
            }
        } else {
            Ok(None)
        }
    }

    fn is_known_type(frame_type: HFrameType) -> bool {
        frame_type == HFrameType(WT_FRAME_CLOSE_SESSION)
    }
}
