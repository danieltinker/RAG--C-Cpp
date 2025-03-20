import streamlit as st
import clang.cindex
import os
import time
import logging

from config import OPENAI_API_KEY, EMBEDDING_MODEL, find_libclang
from controllers.symbol_extractor import extract_symbols_from_directory
from services.embedding_service import EmbeddingService
from controllers.index_builder import IndexBuilder
from services.search_service import SearchService
from utils.formatter import print_formatted_function_streamlit

from clang.cindex import LibclangError

# Configure logging (optional)
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s: %(message)s")

st.title("Codebase Semantic Search")

# === Libclang Configuration Section ===
st.header("Configure libclang")

# Automatically detect libclang using your cross-platform logic.
detected_libclang = find_libclang()
if detected_libclang:
    # Pre-fill the text input with the detected path.
    libclang_path = st.text_input("Path to libclang library", value=detected_libclang)
else:
    # If not detected, let the user provide it.
    libclang_path = st.text_input(
        "Path to libclang library", 
        value="",
        help="Libclang was not detected automatically. Please enter the full path to the library (.dll, .so, or .dylib)."
    )

# Attempt to set the libclang configuration if a path is provided.
if libclang_path:
    try:
        clang.cindex.Config.set_library_file(libclang_path)
        clang.cindex.Config.set_compatibility_check(False)
        st.success(f"Using libclang from: {libclang_path}")
    except Exception as e:
        st.error(f"Error setting libclang: {e}")

# === Step 1: Build the Index ===
st.header("Build Index")
dir_path = st.text_input("Enter the path to your codebase directory:", value=".")

if st.button("Build Index"):
    if not os.path.isdir(dir_path):
        st.error("Directory not found. Please check the path.")
    else:
        with st.spinner("Extracting symbols and building index..."):
            start_time = time.time()
            # Create a placeholder for displaying libclang status.
            libclang_status_placeholder = st.empty()
            try:
                symbols = extract_symbols_from_directory(dir_path)
                # If extraction is successful, update the placeholder.
                libclang_status_placeholder.success("Libclang path found successfully :)")
            except LibclangError:
                # Update the placeholder with the error message.
                libclang_status_placeholder.error("Libclang Path Is Invalid, please find it on your computer (see LLVM Installation, README file)")
                st.stop()  # Stop execution until the user updates the libclang path.
                
            if not symbols:
                st.error("No symbols found. Check the extraction method or your codebase contents.")
            else:
                embedding_service = EmbeddingService(OPENAI_API_KEY, EMBEDDING_MODEL)
                index_builder = IndexBuilder(embedding_service, batch_size=1000)
                index = index_builder.build_index(symbols)
                st.session_state["index"] = index
                elapsed = time.time() - start_time
                st.success(f"Index built in {elapsed:.2f} seconds with {len(index)} snippets.")

# === Step 2: Search the Index ===
st.header("Free Text Search Interface")
query = st.text_input("Enter your search query:")

# Add a numeric input for the number of top results.
top_k = st.number_input("Number of top results:", min_value=1, value=3, step=1)

if st.button("Search"):
    if "index" not in st.session_state:
        st.error("Index not built yet. Please build the index first.")
    elif not query.strip():
        st.error("Please enter a valid query.")
    else:
        embedding_service = EmbeddingService(OPENAI_API_KEY, EMBEDDING_MODEL)
        search_service = SearchService(embedding_service)
        results = search_service.search(query, st.session_state["index"], top_k=int(top_k))
        if not results:
            st.warning("No matching results found.")
        else:
            st.subheader("Top Results:")
            for score, item in results:
                st.markdown(f"**Score:** {score:.3f}")
                st.markdown(f"**Type:** {item['type']} | **Name:** {item['name']} | **File:** {item['file']}")
                print_formatted_function_streamlit(item['name'], item['snippet'], language='cpp')
                st.markdown("---")
