/*
 * ShiftDex - unpack, edit and repack APK and JAR files
 * Copyright (C) 2026 palaziks
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
package io.github.palazik.shiftdex;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.TreeSet;
import java.util.regex.Pattern;
import java.util.spi.ToolProvider;
import java.util.stream.Stream;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipException;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

/**
 * The Java side of ShiftDex: everything that deals with JAR archives and
 * class files.
 *
 * <pre>
 *   unpack &lt;archive&gt; &lt;dir&gt; &lt;info.tsv&gt;
 *   pack   &lt;dir&gt; &lt;archive&gt; &lt;info.tsv&gt; [--strip-signatures]
 *   disasm &lt;file.class&gt;
 * </pre>
 *
 * info.tsv records, one entry per line, whether the entry was stored (S)
 * or deflated (D) and its name. Packing uses it to keep the original
 * entry order and compression.
 */
public final class JarTool {

    private static final String MANIFEST = "META-INF/MANIFEST.MF";
    private static final Pattern SIGNATURE =
            Pattern.compile("^META-INF/([^/]+\\.(SF|RSA|DSA|EC)|SIG-[^/]+)$", Pattern.CASE_INSENSITIVE);

    private JarTool() {
    }

    public static void main(String[] args) {
        try {
            if (args.length >= 4 && args[0].equals("unpack")) {
                unpack(Paths.get(args[1]), Paths.get(args[2]), Paths.get(args[3]));
            } else if (args.length >= 4 && args[0].equals("pack")) {
                boolean strip = args.length > 4 && args[4].equals("--strip-signatures");
                pack(Paths.get(args[1]), Paths.get(args[2]), Paths.get(args[3]), strip);
            } else if (args.length == 2 && args[0].equals("disasm")) {
                System.exit(disassemble(Paths.get(args[1])));
            } else {
                System.err.println("usage: unpack <archive> <dir> <info.tsv>");
                System.err.println("       pack <dir> <archive> <info.tsv> [--strip-signatures]");
                System.err.println("       disasm <file.class>");
                System.exit(2);
            }
        } catch (ZipException e) {
            System.out.println("Not a valid archive: " + e.getMessage());
            System.exit(1);
        } catch (IOException | RuntimeException e) {
            System.out.println(e.getClass().getSimpleName() + ": " + e.getMessage());
            System.exit(1);
        }
    }

    /** Returns the cleaned entry name, or null if it would escape the output folder. */
    static String safeName(String name) {
        String normal = name.replace('\\', '/');
        if (normal.startsWith("/") || normal.matches("^[A-Za-z]:.*")) {
            return null;
        }
        List<String> parts = new ArrayList<>();
        for (String part : normal.split("/")) {
            if (part.isEmpty() || part.equals(".")) {
                continue;
            }
            if (part.equals("..")) {
                return null;
            }
            parts.add(part);
        }
        return parts.isEmpty() ? null : String.join("/", parts);
    }

    static void unpack(Path archive, Path dir, Path info) throws IOException {
        Files.createDirectories(dir);
        Path root = dir.toRealPath();
        int count = 0;
        try (ZipFile zip = new ZipFile(archive.toFile());
             BufferedWriter out = Files.newBufferedWriter(info, StandardCharsets.UTF_8)) {
            System.out.println("Extracting " + zip.size() + " entries ...");
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                if (entry.isDirectory()) {
                    continue;
                }
                String name = safeName(entry.getName());
                Path target = name == null ? null : root.resolve(name).normalize();
                if (target == null || !target.startsWith(root) || name.indexOf('\n') >= 0) {
                    System.out.println("Skipped unsafe entry: " + entry.getName());
                    continue;
                }
                Files.createDirectories(target.getParent());
                try (InputStream in = new BufferedInputStream(zip.getInputStream(entry));
                     OutputStream file = new BufferedOutputStream(Files.newOutputStream(target))) {
                    in.transferTo(file);
                }
                out.write(entry.getMethod() == ZipEntry.STORED ? "S\t" : "D\t");
                out.write(name);
                out.newLine();
                count++;
            }
        }
        System.out.println("Extracted " + count + " files.");
    }

    static Map<String, Boolean> readInfo(Path info) throws IOException {
        Map<String, Boolean> known = new LinkedHashMap<>();
        if (!Files.isRegularFile(info)) {
            return known;
        }
        try (BufferedReader reader = Files.newBufferedReader(info, StandardCharsets.UTF_8)) {
            String line;
            while ((line = reader.readLine()) != null) {
                int tab = line.indexOf('\t');
                if (tab == 1) {
                    known.put(line.substring(2), line.charAt(0) == 'S');
                }
            }
        }
        return known;
    }

    static void pack(Path dir, Path archive, Path info, boolean stripSignatures) throws IOException {
        Path root = dir.toRealPath();
        Map<String, Boolean> known = readInfo(info);

        Set<String> present = new TreeSet<>();
        try (Stream<Path> walk = Files.walk(root)) {
            walk.filter(Files::isRegularFile)
                .forEach(p -> present.add(root.relativize(p).toString().replace('\\', '/')));
        }

        // the JAR specification wants the manifest to be the first entry
        Set<String> ordered = new LinkedHashSet<>();
        if (present.contains(MANIFEST)) {
            ordered.add(MANIFEST);
        }
        for (String name : known.keySet()) {
            if (present.contains(name)) {
                ordered.add(name);
            }
        }
        ordered.addAll(present);

        int written = 0;
        int skipped = 0;
        System.out.println("Packing " + ordered.size() + " files ...");
        try (ZipOutputStream zip = new ZipOutputStream(
                new BufferedOutputStream(Files.newOutputStream(archive)), StandardCharsets.UTF_8)) {
            for (String name : ordered) {
                if (stripSignatures && SIGNATURE.matcher(name).matches()) {
                    skipped++;
                    continue;
                }
                Path file = root.resolve(name);
                ZipEntry entry = new ZipEntry(name);
                entry.setTime(Files.getLastModifiedTime(file).toMillis());
                if (Boolean.TRUE.equals(known.get(name))) {
                    // stored entries need size and checksum up front
                    byte[] data = Files.readAllBytes(file);
                    CRC32 crc = new CRC32();
                    crc.update(data);
                    entry.setMethod(ZipEntry.STORED);
                    entry.setSize(data.length);
                    entry.setCompressedSize(data.length);
                    entry.setCrc(crc.getValue());
                    zip.putNextEntry(entry);
                    zip.write(data);
                } else {
                    entry.setMethod(ZipEntry.DEFLATED);
                    zip.putNextEntry(entry);
                    Files.copy(file, zip);
                }
                zip.closeEntry();
                written++;
            }
        }
        if (skipped > 0) {
            System.out.println("Left out " + skipped
                    + " signature file(s); the old signature no longer matches.");
        }
        System.out.println("Packed " + written + " files.");
    }

    /** Prints the bytecode of a class file using the JDK's javap. */
    static int disassemble(Path classFile) {
        Optional<ToolProvider> javap = ToolProvider.findFirst("javap");
        if (!javap.isPresent()) {
            System.out.println("// Disassembly needs a full JDK (javap). Only a Java runtime was found.");
            return 3;
        }
        PrintWriter out = new PrintWriter(System.out, true);
        return javap.get().run(out, out, "-p", "-c", "-constants", classFile.toString());
    }
}
